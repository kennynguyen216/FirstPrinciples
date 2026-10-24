#include "Tests/TestFramework.h"
#include "Engine/Engine.h"
#include "Engine/SceneSerializer.h"

using namespace FP;

namespace
{
	struct HealthComponent
	{
		int32 Value = 100;
		float Regen = 0.5f;
		bool Invulnerable = false;
		String Label = "hp";
	};

	struct TagComponent
	{
	};

	class CounterSystem : public System
	{
	public:
		const char* GetName() const override { return "Counter"; }
		void OnStart(World&) override { ++Starts; }
		void Update(World& world, float) override
		{
			++Updates;
			for (auto [entity, health] : world.View<HealthComponent>().each())
			{
				if (health.Value <= 0)
					world.DestroyEntity(entity);
			}
		}
		int Starts = 0;
		int Updates = 0;
	};

	void RegisterTestComponents()
	{
		Engine::RegisterEngineComponents();
		ComponentRegistry& registry = ComponentRegistry::Get();
		if (registry.Find<HealthComponent>())
			return;
		registry.Register<HealthComponent>("TestHealth")
			.Field("Value", &HealthComponent::Value)
			.Field("Regen", &HealthComponent::Regen)
			.Field("Invulnerable", &HealthComponent::Invulnerable)
			.Field("Label", &HealthComponent::Label);
		registry.Register<TagComponent>("TestTag");
	}
}

FP_TEST(World_EntitiesAndComponents)
{
	StrongObjectPtr<World> world(NewObject<World>());
	Entity a = world->CreateEntity("A");
	Entity b = world->CreateEntity("B");
	world->Add<HealthComponent>(a).Value = 5;
	world->Add<TagComponent>(b);

	FP_EXPECT(world->Has<TransformComponent>(a));
	FP_EXPECT(world->Get<HealthComponent>(a).Value == 5);
	FP_EXPECT(world->Has<TagComponent>(b));
	FP_EXPECT(world->FindEntityByName("B") == b);
	FP_EXPECT(world->GetEntities().Num() == 2);
	FP_EXPECT(world->GetEntities()[0] == a);

	int count = 0;
	for (auto [entity, health] : world->View<HealthComponent>().each())
		count += health.Value;
	FP_EXPECT(count == 5);

	world->DestroyEntity(a);
	FP_EXPECT(!world->IsValid(a));
}

FP_TEST(World_SystemsRunOnlyWhilePlaying)
{
	StrongObjectPtr<World> world(NewObject<World>());
	CounterSystem* counter = world->AddSystem<CounterSystem>();
	Entity dying = world->CreateEntity("Dying");
	world->Add<HealthComponent>(dying).Value = 0;

	world->Update(0.016f);
	FP_EXPECT(counter->Updates == 0);

	world->BeginPlay();
	FP_EXPECT(counter->Starts == 1);
	world->Update(0.016f);
	FP_EXPECT(counter->Updates == 1);
	FP_EXPECT(!world->IsValid(dying)); // deferred destroy flushed at end of Update
	world->EndPlay();
}

FP_TEST(Scene_RoundTrip)
{
	RegisterTestComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	Entity e = world->CreateEntity("Hero \"One\"");
	world->Get<TransformComponent>(e).Position = { 1.5f, -2, 3 };
	HealthComponent& health = world->Add<HealthComponent>(e);
	health.Value = 42;
	health.Invulnerable = true;
	health.Label = "with space";
	world->Add<TagComponent>(e);
	world->Add<SpriteComponent>(e).Tint = { 0.25f, 0.5f, 0.75f, 1 };
	const Guid id = world->Get<IdComponent>(e).Id;

	const String text = SceneSerializer::SaveToString(*world.Get());

	StrongObjectPtr<World> loaded(NewObject<World>());
	FP_EXPECT(SceneSerializer::LoadFromString(*loaded.Get(), text.View()));
	Entity l = loaded->FindEntityByGuid(id);
	FP_EXPECT(loaded->IsValid(l));
	FP_EXPECT(loaded->GetEntityName(l) == "Hero \"One\"");
	FP_EXPECT(loaded->Get<TransformComponent>(l).Position == Vec3(1.5f, -2, 3));
	FP_EXPECT(loaded->Get<HealthComponent>(l).Value == 42);
	FP_EXPECT(loaded->Get<HealthComponent>(l).Invulnerable);
	FP_EXPECT(loaded->Get<HealthComponent>(l).Label == "with space");
	FP_EXPECT(loaded->Has<TagComponent>(l));
	FP_EXPECT(loaded->Get<SpriteComponent>(l).Tint.B == 0.75f);
	FP_EXPECT(SceneSerializer::SaveToString(*loaded.Get()) == text);
}

FP_TEST(Scene_CopiesOneEntity)
{
	RegisterTestComponents();
	StrongObjectPtr<World> source(NewObject<World>());
	const Entity original = source->CreateEntity("Hero");
	source->Get<TransformComponent>(original).Position = { 1, 2, 3 };
	source->Add<HealthComponent>(original).Value = 42;
	source->CreateEntity("Other");

	const String text = SceneSerializer::SaveEntityToString(*source.Get(), original);
	FP_EXPECT(!text.Contains("Other"));

	StrongObjectPtr<World> target(NewObject<World>());
	target->CreateEntity("Already here");
	const Entity copy = SceneSerializer::LoadEntityFromString(*target.Get(), text.View());
	FP_EXPECT(copy != NullEntity);
	FP_EXPECT(target->GetEntities().Num() == 2);
	FP_EXPECT(target->GetEntityName(copy) == "Hero");
	FP_EXPECT(target->Get<HealthComponent>(copy).Value == 42);
	FP_EXPECT(target->Get<TransformComponent>(copy).Position.Z == 3);

	// The same id twice, or broken text, adds nothing.
	FP_EXPECT(SceneSerializer::LoadEntityFromString(*target.Get(), text.View()) == NullEntity);
	FP_EXPECT(SceneSerializer::LoadEntityFromString(*target.Get(), "entity nope\n") == NullEntity);
	FP_EXPECT(target->GetEntities().Num() == 2);
}

FP_TEST(Scene_SkipsUnknownData)
{
	RegisterTestComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	const char* text =
		"entity 00000000-0000-4000-8000-000000000001\n"
		"component Name\n  Name \"Kept\"\n  Bogus 1\n"
		"component DoesNotExist\n  Value 3\n";
	FP_EXPECT(SceneSerializer::LoadFromString(*world.Get(), text));
	FP_EXPECT(world->GetEntities().Num() == 1);
	FP_EXPECT(world->GetEntityName(world->GetEntities()[0]) == "Kept");
}

FP_TEST(Scene_MigratesVersion1Transforms)
{
	RegisterTestComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	const char* text =
		"version 1\n"
		"entity 00000000-0000-4000-8000-000000000002\n"
		"component Transform\n  Position 1 2\n  Rotation 45\n  Scale 2 3\n";
	FP_EXPECT(SceneSerializer::LoadFromString(*world.Get(), text));
	const TransformComponent& t = world->Get<TransformComponent>(world->GetEntities()[0]);
	FP_EXPECT(t.Position == Vec3(1, 2, 0));
	FP_EXPECT(t.Rotation == Vec3(0, 0, 45));
	FP_EXPECT(t.Scale == Vec3(2, 3, 1));
}

namespace
{
	bool Near(Vec3 a, Vec3 b) { return (a - b).Length() < 1e-4f; }
}

FP_TEST(Math_TransformAndInverse)
{
	TransformComponent t;
	t.Position = { 1, 2, 3 };
	t.Rotation = { 10, 20, 30 };
	t.Scale = { 2, 2, 2 };
	const Mat4 m = t.GetMatrix();
	const Vec3 p{ 0.5f, -1, 4 };
	FP_EXPECT(Near(m.Inverse().TransformPoint(m.TransformPoint(p)), p));

	// Pure roll matches the old 2D rotation.
	TransformComponent roll;
	roll.Rotation = { 0, 0, 90 };
	FP_EXPECT(Near(roll.GetMatrix().TransformVector({ 1, 0, 0 }), { 0, 1, 0 }));
	FP_EXPECT(Near(roll.GetForward(), { 0, 0, -1 }));

	// Perspective maps the near plane to depth 0 and the far plane to depth 1.
	const Mat4 proj = Mat4::Perspective(60, 1, 0.1f, 100);
	const Vec4 nearClip = proj * Vec4(0, 0, -0.1f, 1);
	const Vec4 farClip = proj * Vec4(0, 0, -100, 1);
	FP_EXPECT(std::fabs(nearClip.Z / nearClip.W) < 1e-4f);
	FP_EXPECT(std::fabs(farClip.Z / farClip.W - 1) < 1e-4f);
}

FP_TEST(Math_QuatToEulerRoundTrips)
{
	const Vec3 angles[] = { { 0, 0, 0 }, { 10, 20, 30 }, { -45, 170, -120 }, { 89, -30, 60 }, { 0, 0, 90 }, { 90, 40, 0 } };
	for (const Vec3& euler : angles)
	{
		const Quat q = Quat::FromEuler(euler);
		const Quat back = Quat::FromEuler(q.ToEuler());
		// Same rotation, even when the angles come back as a different but equal set.
		for (const Vec3& v : { Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1) })
			FP_EXPECT(Near(back.Rotate(v), q.Rotate(v)));
	}
	// Angles in range come back unchanged.
	FP_EXPECT(Near(Quat::FromEuler({ 10, 20, 30 }).ToEuler(), { 10, 20, 30 }));
}

#include "Engine/Render/MeshData.h"

FP_TEST(Mesh_PrimitivesAreWellFormed)
{
	for (const char* name : Primitives::GetNames())
	{
		MeshData mesh;
		FP_EXPECT(Primitives::Create(name, mesh));
		FP_EXPECT(mesh.Indices.Num() % 3 == 0);
		for (uint32 index : mesh.Indices)
			FP_EXPECT(index < static_cast<uint32>(mesh.Vertices.Num()));
		for (const MeshVertex& v : mesh.Vertices)
		{
			FP_EXPECT(std::fabs(v.Normal.Length() - 1) < 1e-3f);
			FP_EXPECT(std::fabs(Vec3::Dot(v.Normal, v.Tangent.XYZ())) < 1e-3f);
		}

		// Counter clockwise from outside means each face normal agrees with its vertex normals.
		for (int32 i = 0; i < mesh.Indices.Num(); i += 3)
		{
			const MeshVertex& a = mesh.Vertices[static_cast<int32>(mesh.Indices[i])];
			const MeshVertex& b = mesh.Vertices[static_cast<int32>(mesh.Indices[i + 1])];
			const MeshVertex& c = mesh.Vertices[static_cast<int32>(mesh.Indices[i + 2])];
			const Vec3 face = Vec3::Cross(b.Position - a.Position, c.Position - a.Position);
			if (face.LengthSquared() > 1e-10f)
				FP_EXPECT(Vec3::Dot(face, a.Normal + b.Normal + c.Normal) > 0);
		}
	}
	MeshData cube = Primitives::Cube();
	FP_EXPECT(cube.BoundsMin == Vec3(-0.5f, -0.5f, -0.5f) && cube.BoundsMax == Vec3(0.5f, 0.5f, 0.5f));
}

FP_TEST(Mesh_ParsesObj)
{
	const char* obj =
		"# square made of a quad and no normals\n"
		"v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
		"vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
		"f 1/1 2/2 3/3 4/4\n";
	MeshData mesh;
	FP_EXPECT(ParseObj(obj, mesh));
	FP_EXPECT(mesh.Vertices.Num() == 4);
	FP_EXPECT(mesh.Indices.Num() == 6);
	FP_EXPECT(Near(mesh.Vertices[0].Normal, { 0, 0, 1 }));
	FP_EXPECT(Near(mesh.Vertices[0].Tangent.XYZ(), { 1, 0, 0 }));
	FP_EXPECT(mesh.Vertices[0].UV == Vec2(0, 1)); // flipped to top left origin
	FP_EXPECT(!ParseObj("f 1 2 3\n", mesh));
}

#include "Engine/SceneHistory.h"

FP_TEST(SceneHistory_UndoRedo)
{
	RegisterTestComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	Entity e = world->CreateEntity("Mover");
	const Guid id = world->Get<IdComponent>(e).Id;
	SceneHistory history(*world.Get());
	history.Reset(id);
	FP_EXPECT(!history.CanUndo());
	FP_EXPECT(!history.Commit(id)); // nothing changed

	world->Get<TransformComponent>(e).Position.X = 5;
	FP_EXPECT(history.Commit(id));
	world->CreateEntity("Second");
	FP_EXPECT(history.Commit(Guid()));

	FP_EXPECT(history.Undo());
	FP_EXPECT(world->GetEntities().Num() == 1);
	FP_EXPECT(history.GetSelection() == id);
	FP_EXPECT(history.Undo());
	FP_EXPECT(world->Get<TransformComponent>(world->FindEntityByGuid(id)).Position.X == 0);
	FP_EXPECT(!history.Undo());

	FP_EXPECT(history.Redo());
	FP_EXPECT(world->Get<TransformComponent>(world->FindEntityByGuid(id)).Position.X == 5);

	// A new edit after undo drops the steps that were undone.
	world->Get<TransformComponent>(world->FindEntityByGuid(id)).Position.Y = 2;
	FP_EXPECT(history.Commit(id));
	FP_EXPECT(!history.CanRedo());
	FP_EXPECT(history.Undo());
	FP_EXPECT(world->GetEntities().Num() == 1);
	FP_EXPECT(world->Get<TransformComponent>(world->FindEntityByGuid(id)).Position.Y == 0);
}

FP_TEST(SceneHistory_DropsOldestSteps)
{
	StrongObjectPtr<World> world(NewObject<World>());
	Entity e = world->CreateEntity("Counter");
	SceneHistory history(*world.Get());
	history.Reset();
	for (int32 i = 1; i <= SceneHistory::MaxSteps + 5; ++i)
	{
		world->Get<TransformComponent>(e).Position.X = static_cast<float>(i);
		history.Commit(Guid());
	}
	int32 undone = 0;
	while (history.Undo())
		++undone;
	FP_EXPECT(undone == SceneHistory::MaxSteps);
	FP_EXPECT(world->Get<TransformComponent>(world->GetEntities()[0]).Position.X == 5);
}

#include "Engine/Material.h"

FP_TEST(SceneHistory_UndoesMaterialEdits)
{
	StrongObjectPtr<World> world(NewObject<World>());
	SceneHistory history(*world.Get());
	history.Reset();
	// Loaded after the reset, like opening a material in the inspector.
	Material* material = MaterialLibrary::Get().Load("Materials/Bricks.material");
	FP_EXPECT(material);
	if (!material)
		return;
	const String original = material->SaveToString();
	material->GetData().Roughness = 0.123f;
	FP_EXPECT(history.Commit(Guid()));
	FP_EXPECT(history.Undo());
	FP_EXPECT(material->SaveToString() == original);
	FP_EXPECT(history.Redo());
	FP_EXPECT(material->GetData().Roughness == 0.123f);

	// Put it back so nothing in the test can write the edit to disk.
	material->Load();
}
