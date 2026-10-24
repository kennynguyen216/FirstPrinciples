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
	world->Get<TransformComponent>(e).Position = { 1.5f, -2 };
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
	FP_EXPECT(loaded->Get<TransformComponent>(l).Position == Vec2(1.5f, -2));
	FP_EXPECT(loaded->Get<HealthComponent>(l).Value == 42);
	FP_EXPECT(loaded->Get<HealthComponent>(l).Invulnerable);
	FP_EXPECT(loaded->Get<HealthComponent>(l).Label == "with space");
	FP_EXPECT(loaded->Has<TagComponent>(l));
	FP_EXPECT(loaded->Get<SpriteComponent>(l).Tint.B == 0.75f);
	FP_EXPECT(SceneSerializer::SaveToString(*loaded.Get()) == text);
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
