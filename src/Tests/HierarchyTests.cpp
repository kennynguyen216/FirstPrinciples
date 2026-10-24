#include "Tests/TestFramework.h"
#include "Engine/Engine.h"
#include "Engine/SceneSerializer.h"

using namespace FP;

namespace
{
	bool Near(Vec3 a, Vec3 b) { return (a - b).Length() < 1e-4f; }
}

FP_TEST(Hierarchy_ChildrenFollowTheirParent)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	const Entity car = world->CreateEntity("Car");
	const Entity wheel = world->CreateEntity("Wheel");
	const Entity bolt = world->CreateEntity("Bolt");
	world->Get<TransformComponent>(car).Position = { 10, 0, 0 };
	world->Get<TransformComponent>(wheel).Position = { 11, 0, 0 };

	// Keeping the world transform turns the wheel's position into an offset from the car.
	FP_EXPECT(world->SetParent(wheel, car));
	FP_EXPECT(world->SetParent(bolt, wheel, false));
	FP_EXPECT(Near(world->Get<TransformComponent>(wheel).Position, { 1, 0, 0 }));
	FP_EXPECT(Near(world->GetWorldPosition(wheel), { 11, 0, 0 }));
	FP_EXPECT(world->GetParent(wheel) == car && world->GetParent(car) == NullEntity);
	FP_EXPECT(world->GetChildren(car).Num() == 1 && world->GetChildren(car)[0] == wheel);
	FP_EXPECT(world->FindChild(car, "Bolt") == NullEntity && world->FindChild(car, "Bolt", true) == bolt);
	FP_EXPECT(world->IsDescendantOf(bolt, car));

	// Moving and turning the parent carries the children along.
	TransformComponent& carTransform = world->Get<TransformComponent>(car);
	carTransform.Rotation = { 0, 90, 0 };
	carTransform.Scale = { 2, 2, 2 };
	FP_EXPECT(Near(world->GetWorldPosition(wheel), { 10, 0, -2 }));
	FP_EXPECT(Near(world->GetWorldTransform(bolt).Rotation, { 0, 90, 0 }));
	FP_EXPECT(Near(world->GetWorldTransform(bolt).Scale, { 2, 2, 2 }));

	// Setting a world position goes through the parents.
	world->SetWorldPosition(bolt, { 0, 0, 0 });
	FP_EXPECT(Near(world->GetWorldPosition(bolt), { 0, 0, 0 }));

	// So does a whole world transform, the way the editor's gizmo moves children.
	TransformComponent placed;
	placed.Position = { 3, 4, 5 };
	placed.Rotation = { 0, 30, 0 };
	placed.Scale = { 4, 4, 4 };
	world->SetWorldTransform(bolt, placed);
	FP_EXPECT(Near(world->GetWorldPosition(bolt), { 3, 4, 5 }));
	FP_EXPECT(Near(world->GetWorldTransform(bolt).Rotation, { 0, 30, 0 }));
	FP_EXPECT(Near(world->Get<TransformComponent>(bolt).Scale, { 2, 2, 2 }));

	// No loops, and detaching keeps the wheel where it is.
	FP_EXPECT(!world->SetParent(car, bolt));
	FP_EXPECT(!world->SetParent(car, car));
	FP_EXPECT(world->SetParent(wheel, NullEntity));
	FP_EXPECT(Near(world->GetWorldPosition(wheel), { 10, 0, -2 }));
	FP_EXPECT(Near(world->Get<TransformComponent>(wheel).Rotation, { 0, 90, 0 }));

	// Destroying a parent destroys its children too.
	world->DestroyEntity(wheel);
	FP_EXPECT(!world->IsValid(bolt) && world->IsValid(car));
}

FP_TEST(Hierarchy_SavesAndLoadsParents)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	// The child is created first so loading has to find a parent that comes later.
	const Entity child = world->CreateEntity("Child");
	const Entity parent = world->CreateEntity("Parent");
	world->Get<TransformComponent>(parent).Position = { 0, 5, 0 };
	world->SetParent(child, parent, false);

	const String text = SceneSerializer::SaveToString(*world.Get());
	FP_EXPECT(text.Contains("component Parent"));
	StrongObjectPtr<World> loaded(NewObject<World>());
	FP_EXPECT(SceneSerializer::LoadFromString(*loaded.Get(), text.View()));
	const Entity loadedChild = loaded->FindEntityByName("Child");
	FP_EXPECT(loaded->GetParent(loadedChild) == loaded->FindEntityByName("Parent"));
	FP_EXPECT(Near(loaded->GetWorldPosition(loadedChild), { 0, 5, 0 }));
}

FP_TEST(Hierarchy_CopiesWholeBranches)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	const Entity table = world->CreateEntity("Table");
	const Entity leg = world->CreateEntity("Leg");
	const Entity foot = world->CreateEntity("Foot");
	world->CreateEntity("Unrelated");
	world->SetParent(leg, table);
	world->SetParent(foot, leg);

	// Copying the table brings its leg and foot along, with new ids and the same shape.
	const String text = SceneSerializer::SaveEntityTreeToString(*world.Get(), table);
	FP_EXPECT(!text.Contains("Unrelated"));
	const Array<Entity> copies = SceneSerializer::LoadEntitiesAsCopies(*world.Get(), text.View());
	FP_EXPECT(copies.Num() == 3);
	FP_EXPECT(world->GetEntities().Num() == 7);
	FP_EXPECT(copies[0] != table && world->GetEntityName(copies[0]) == "Table");
	FP_EXPECT(world->GetParent(copies[0]) == NullEntity);
	FP_EXPECT(world->GetParent(copies[1]) == copies[0] && world->GetParent(copies[2]) == copies[1]);
	FP_EXPECT(world->GetChildren(table).Num() == 1);

	// Pasting a leg alone keeps it under the original table, and into a world without it, top level.
	const String legText = SceneSerializer::SaveEntityTreeToString(*world.Get(), leg);
	const Array<Entity> legCopy = SceneSerializer::LoadEntitiesAsCopies(*world.Get(), legText.View());
	FP_EXPECT(legCopy.Num() == 2 && world->GetParent(legCopy[0]) == table);
	StrongObjectPtr<World> other(NewObject<World>());
	const Array<Entity> moved = SceneSerializer::LoadEntitiesAsCopies(*other.Get(), legText.View());
	FP_EXPECT(moved.Num() == 2 && other->GetParent(moved[0]) == NullEntity && !other->Has<ParentComponent>(moved[0]));
	FP_EXPECT(SceneSerializer::LoadEntitiesAsCopies(*other.Get(), "not a scene").IsEmpty());
}
