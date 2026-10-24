#include "Tests/TestFramework.h"
#include "Engine/Engine.h"
#include "Engine/Physics/PhysicsSystem.h"
#include "Engine/Physics/PhysicsWorld.h"

#include <cmath>

using namespace FP;

namespace
{
	// A 20 by 1 by 20 floor with its top at y = 0.
	Entity MakeFloor(World& world)
	{
		const Entity floor = world.CreateEntity("Floor");
		world.Get<TransformComponent>(floor).Position = { 0, -0.5f, 0 };
		world.Add<ColliderComponent>(floor).Size = { 20, 1, 20 };
		return floor;
	}

	Entity MakeBox(World& world, Vec3 position)
	{
		const Entity box = world.CreateEntity("Box");
		world.Get<TransformComponent>(box).Position = position;
		world.Add<ColliderComponent>(box);
		world.Add<RigidBodyComponent>(box);
		return box;
	}

	void Run(World& world, float seconds)
	{
		for (float t = 0; t < seconds; t += PhysicsWorld::FixedStep)
			world.Update(PhysicsWorld::FixedStep);
	}
}

FP_TEST(Physics_BoxesFallAndLand)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	MakeFloor(*world.Get());
	const Entity box = MakeBox(*world.Get(), { 0, 5, 0 });
	world->AddSystem<PhysicsSystem>();
	world->BeginPlay();
	FP_EXPECT(static_cast<PhysicsWorld&>(world->GetPhysics()).GetBodyCount() == 2);

	// Half a second in, it is falling at about g t.
	Run(*world.Get(), 0.5f);
	FP_EXPECT(world->GetPhysics().GetVelocity(box).Y < -4);
	FP_EXPECT(world->Get<TransformComponent>(box).Position.Y < 4);

	// Later it rests on the floor, its center half a box above it.
	Run(*world.Get(), 2.5f);
	FP_EXPECT(std::fabs(world->Get<TransformComponent>(box).Position.Y - 0.5f) < 0.05f);
	FP_EXPECT(world->GetPhysics().GetVelocity(box).Length() < 0.1f);

	// Stopping drops the bodies.
	world->EndPlay();
	FP_EXPECT(static_cast<PhysicsWorld&>(world->GetPhysics()).GetBodyCount() == 0);
}

FP_TEST(Physics_QueriesAndForces)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	const Entity floor = MakeFloor(*world.Get());
	const Entity box = MakeBox(*world.Get(), { 3, 0.5f, 0 });
	world->Get<RigidBodyComponent>(box).Friction = 0;
	const Entity zone = world->CreateEntity("Zone");
	world->Add<ColliderComponent>(zone).Trigger = true;
	world->Get<TransformComponent>(zone).Position = { -3, 1, 0 };
	world->AddSystem<PhysicsSystem>();
	world->BeginPlay();
	Physics& physics = world->GetPhysics();

	// Straight down from above the box hits its top. Rays pass through triggers.
	RaycastHit hit;
	FP_EXPECT(physics.Raycast({ 3, 10, 0 }, { 0, -1, 0 }, 100, hit));
	FP_EXPECT(hit.Entity == box && std::fabs(hit.Point.Y - 1) < 0.02f && hit.Normal.Y > 0.99f);
	FP_EXPECT(physics.Raycast({ -3, 10, 0 }, { 0, -1, 0 }, 100, hit) && hit.Entity == floor);
	FP_EXPECT(physics.Raycast({ 3, 10, 0 }, { 0, -1, 0 }, 100, hit, box) && hit.Entity == floor);
	FP_EXPECT(!physics.Raycast({ 3, 10, 0 }, { 0, 1, 0 }, 100, hit));

	// Overlaps find triggers too.
	const Array<Entity> near = physics.OverlapSphere({ -3, 1, 0 }, 0.2f);
	FP_EXPECT(near.Num() == 1 && near[0] == zone);

	// An impulse of 2 newton seconds on a 1 kilogram box makes it move at 2 meters per second.
	physics.AddImpulse(box, { 2, 0, 0 });
	world->Update(PhysicsWorld::FixedStep);
	FP_EXPECT(std::fabs(physics.GetVelocity(box).X - 2) < 0.1f);

	// Moving the transform by hand teleports the body.
	world->Get<TransformComponent>(box).Position = { 0, 4, 5 };
	world->Update(PhysicsWorld::FixedStep);
	FP_EXPECT(std::fabs(world->Get<TransformComponent>(box).Position.Z - 5) < 0.01f);

	// Changing the collider rebuilds the body, here twice as tall so it rests higher.
	world->Get<ColliderComponent>(box).Size = { 1, 2, 1 };
	Run(*world.Get(), 3);
	FP_EXPECT(std::fabs(world->Get<TransformComponent>(box).Position.Y - 1) < 0.05f);

	// Removing the collider removes the body, and the rest carry on.
	world->Remove<ColliderComponent>(box);
	world->Update(PhysicsWorld::FixedStep);
	FP_EXPECT(!static_cast<PhysicsWorld&>(physics).HasBody(box) && static_cast<PhysicsWorld&>(physics).GetBodyCount() == 2);
	world->EndPlay();
}

FP_TEST(Physics_KinematicBodiesPushAndChildrenFollow)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	MakeFloor(*world.Get());
	const Entity box = MakeBox(*world.Get(), { 0, 0.5f, 0 });
	const Entity pusher = world->CreateEntity("Pusher");
	world->Get<TransformComponent>(pusher).Position = { -2, 0.5f, 0 };
	world->Add<ColliderComponent>(pusher);
	world->Add<RigidBodyComponent>(pusher).Motion = "Kinematic";

	// A dynamic child of a parent that is not physics lands at the right world height.
	const Entity holder = world->CreateEntity("Holder");
	world->Get<TransformComponent>(holder).Position = { 5, 3, 0 };
	const Entity child = MakeBox(*world.Get(), { 5, 6, 0 });
	world->SetParent(child, holder);

	world->AddSystem<PhysicsSystem>();
	world->BeginPlay();
	// The kinematic pusher slides right through where the box is and shoves it along.
	for (int i = 0; i < 120; ++i)
	{
		world->Get<TransformComponent>(pusher).Position.X += 2.0f / 60.0f;
		world->Update(PhysicsWorld::FixedStep);
	}
	FP_EXPECT(world->Get<TransformComponent>(box).Position.X > 1);
	FP_EXPECT(std::fabs(world->GetWorldPosition(child).Y - 0.5f) < 0.05f);
	FP_EXPECT(std::fabs(world->Get<TransformComponent>(child).Position.Y + 2.5f) < 0.05f);
	world->EndPlay();
}

#include "Engine/Scripting/Script.h"
#include "Engine/Scripting/ScriptSystem.h"

namespace
{
	// Counts what physics tells it, the way a gameplay script would react.
	class TestContacts : public Script
	{
	public:
		int32 Hits = 0;
		int32 Leaves = 0;
		int32 TriggersIn = 0;
		int32 TriggersOut = 0;
		Vec3 LastNormal;

		void OnCollisionEnter(Entity, const Collision& collision) override
		{
			++Hits;
			LastNormal = collision.Normal;
		}
		void OnCollisionExit(Entity) override { ++Leaves; }
		void OnTriggerEnter(Entity) override { ++TriggersIn; }
		void OnTriggerExit(Entity) override { ++TriggersOut; }
	};
}

FP_SCRIPT_FIELDS(TestContacts) {}

FP_TEST(Physics_ScriptsHearContactsAndTriggers)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	const Entity floor = MakeFloor(*world.Get());
	const Entity ball = MakeBox(*world.Get(), { 0, 3, 0 });
	world->Get<ColliderComponent>(ball).Shape = "Sphere";
	world->Add<ScriptComponent>(ball).Scripts.Emplace("TestContacts");
	// A trigger box hanging in the ball's way, and another one away from it.
	const Entity zone = world->CreateEntity("Zone");
	world->Get<TransformComponent>(zone).Position = { 0, 1.5f, 0 };
	world->Add<ColliderComponent>(zone).Trigger = true;
	world->AddSystem<ScriptSystem>();
	world->AddSystem<PhysicsSystem>();
	world->BeginPlay();
	TestContacts* contacts = static_cast<TestContacts*>(world->Get<ScriptComponent>(ball).Scripts[0].Instance);
	FP_EXPECT(contacts);
	if (!contacts)
		return;

	// The ball falls through the trigger and lands on the floor, once each.
	Run(*world.Get(), 2);
	FP_EXPECT(contacts->TriggersIn == 1);
	FP_EXPECT(contacts->Hits == 1 && contacts->LastNormal.Y > 0.9f);

	// Lifting it off the floor ends the contact.
	world->Get<TransformComponent>(ball).Position = { 0, 8, 10 };
	Run(*world.Get(), 0.1f);
	FP_EXPECT(contacts->Leaves == 1 && contacts->TriggersOut == 1);

	// Destroying the floor under a resting ball still sends the exit.
	world->Get<TransformComponent>(ball).Position = { 3, 0.5f, 0 };
	Run(*world.Get(), 0.5f);
	FP_EXPECT(contacts->Hits == 2);
	world->DestroyEntity(floor);
	Run(*world.Get(), 0.1f);
	FP_EXPECT(contacts->Leaves == 2);
	world->EndPlay();
}
