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

FP_TEST(Physics_InteractionsPushAndFindBodies)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	MakeFloor(*world.Get());
	const Entity near = MakeBox(*world.Get(), { 1, 0.5f, 0 });
	const Entity far = MakeBox(*world.Get(), { 8, 0.5f, 0 });
	const Entity spun = MakeBox(*world.Get(), { 0, 0.5f, 6 });
	world->AddSystem<PhysicsSystem>();
	world->BeginPlay();
	Run(*world.Get(), 0.2f);
	Physics& physics = world->GetPhysics();
	FP_EXPECT(physics.GetMass(near) > 0);

	// An explosion moves only what is in range, away from the center.
	physics.AddExplosion({ 0, 0.5f, 0 }, 4, 20);
	Run(*world.Get(), PhysicsWorld::FixedStep);
	FP_EXPECT(physics.GetVelocity(near).X > 1);
	FP_EXPECT(physics.GetVelocity(far).Length() < 0.1f);

	// A kick on the edge spins as well as moves.
	physics.AddImpulseAtPoint(spun, { 0, 0, -5 }, { 0.5f, 0.5f, 6 });
	Run(*world.Get(), PhysicsWorld::FixedStep);
	FP_EXPECT(physics.GetAngularVelocity(spun).Length() > 0.5f);

	// A ball swept down the far box's column finds it, the floor if it misses.
	RaycastHit hit;
	FP_EXPECT(physics.SphereCast({ 8.3f, 5, 0 }, 0.4f, { 0, -1, 0 }, 10, hit));
	FP_EXPECT(hit.Entity == far && hit.Normal.Y > 0.9f);
	FP_EXPECT(std::fabs(hit.Point.Y - 1) < 0.05f);
	FP_EXPECT(physics.SphereCast({ 8.3f, 5, 0 }, 0.4f, { 0, -1, 0 }, 10, hit, far));
	FP_EXPECT(hit.Entity != far && std::fabs(hit.Point.Y) < 0.05f);
	world->EndPlay();
}

namespace
{
	// Steps physics by hand so the test can see every event.
	void RunCharacters(World& world, PhysicsWorld& physics, float seconds, Array<PhysicsEvent>* events = nullptr, Entity walker = NullEntity, Vec3 walk = {})
	{
		for (float t = 0; t < seconds; t += PhysicsWorld::FixedStep)
		{
			if (walker != NullEntity)
				physics.MoveCharacter(walker, walk);
			physics.Sync(world);
			physics.Step(PhysicsWorld::FixedStep);
			physics.WriteBack(world);
			physics.WriteCharacters(world);
			Array<PhysicsEvent> step;
			physics.TakeEvents(step);
			if (events)
			{
				for (const PhysicsEvent& event : step)
					events->Add(event);
			}
		}
	}

	bool Saw(const Array<PhysicsEvent>& events, PhysicsEvent::Kind kind, Entity a, Entity b)
	{
		return events.FindIf([&](const PhysicsEvent& e) { return e.Type == kind && e.A == a && e.B == b; }) != INDEX_NONE;
	}
}

FP_TEST(Physics_CharactersWalkClimbAndJump)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	PhysicsWorld physics;
	MakeFloor(*world.Get());
	const Entity player = world->CreateEntity("Player");
	world->Get<TransformComponent>(player).Position = { 0, 3, 0 };
	world->Add<CharacterControllerComponent>(player);

	// A low step at x 3 and a tall wall at z -3.
	const Entity step = world->CreateEntity("Step");
	world->Get<TransformComponent>(step).Position = { 4, 0.125f, 0 };
	world->Add<ColliderComponent>(step).Size = { 2, 0.25f, 4 };
	const Entity wall = world->CreateEntity("Wall");
	world->Get<TransformComponent>(wall).Position = { 0, 1, -3 };
	world->Add<ColliderComponent>(wall).Size = { 4, 2, 0.5f };

	// Falls and stands with its 2 unit tall capsule on the floor.
	RunCharacters(*world.Get(), physics, 1.5f);
	FP_EXPECT(physics.GetCharacterCount() == 1);
	FP_EXPECT(physics.IsGrounded(player));
	FP_EXPECT(std::fabs(world->Get<TransformComponent>(player).Position.Y - 1) < 0.05f);

	// Walks onto the step.
	RunCharacters(*world.Get(), physics, 1.0f, nullptr, player, { 4, 0, 0 });
	const Vec3 onStep = world->Get<TransformComponent>(player).Position;
	FP_EXPECT(onStep.X > 3.5f && std::fabs(onStep.Y - 1.25f) < 0.05f);

	// Stops without Move.
	RunCharacters(*world.Get(), physics, 0.3f);
	const float stoppedX = world->Get<TransformComponent>(player).Position.X;
	RunCharacters(*world.Get(), physics, 0.3f);
	FP_EXPECT(std::fabs(world->Get<TransformComponent>(player).Position.X - stoppedX) < 0.01f);

	// Back on the floor, the wall stops it.
	world->Get<TransformComponent>(player).Position = { 0, 1, 0 };
	RunCharacters(*world.Get(), physics, 1.5f, nullptr, player, { 0, 0, -4 });
	FP_EXPECT(world->Get<TransformComponent>(player).Position.Z > -2.3f);

	// Jumps only from the ground.
	FP_EXPECT(physics.JumpCharacter(player, 5));
	RunCharacters(*world.Get(), physics, 0.2f);
	FP_EXPECT(!physics.IsGrounded(player) && world->Get<TransformComponent>(player).Position.Y > 1.5f);
	FP_EXPECT(!physics.JumpCharacter(player, 5));
	RunCharacters(*world.Get(), physics, 1.5f);
	FP_EXPECT(physics.IsGrounded(player));

	// Rays hit it, unless told to skip it.
	RaycastHit hit;
	const Vec3 at = world->Get<TransformComponent>(player).Position;
	FP_EXPECT(physics.Raycast(at + Vec3(0, 5, 0), { 0, -1, 0 }, 10, hit, NullEntity) && hit.Entity == player);
	FP_EXPECT(physics.Raycast(at + Vec3(0, 5, 0), { 0, -1, 0 }, 10, hit, player) && hit.Entity != player);
}

FP_TEST(Physics_CharactersTouchTriggersAndPushCrates)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	PhysicsWorld physics;
	const Entity floor = MakeFloor(*world.Get());
	const Entity player = world->CreateEntity("Player");
	world->Get<TransformComponent>(player).Position = { 0, 1, 0 };
	world->Add<CharacterControllerComponent>(player);
	const Entity zone = world->CreateEntity("Zone");
	world->Get<TransformComponent>(zone).Position = { 3, 1, 0 };
	ColliderComponent& trigger = world->Add<ColliderComponent>(zone);
	trigger.Size = { 1, 2, 2 };
	trigger.Trigger = true;
	const Entity crate = MakeBox(*world.Get(), { 0, 0.5f, 3 });

	Array<PhysicsEvent> events;
	RunCharacters(*world.Get(), physics, 0.5f, &events);
	FP_EXPECT(Saw(events, PhysicsEvent::Kind::CollisionEnter, player, floor));

	// Walking through the zone enters and then leaves it.
	events.Clear();
	RunCharacters(*world.Get(), physics, 2.0f, &events, player, { 3, 0, 0 });
	FP_EXPECT(Saw(events, PhysicsEvent::Kind::TriggerEnter, player, zone));
	FP_EXPECT(Saw(events, PhysicsEvent::Kind::TriggerExit, player, zone));
	FP_EXPECT(!Saw(events, PhysicsEvent::Kind::CollisionEnter, player, zone));

	// Walking into the crate shoves it.
	world->Get<TransformComponent>(player).Position = { 0, 1, 0 };
	RunCharacters(*world.Get(), physics, 2.0f, nullptr, player, { 0, 0, 3 });
	FP_EXPECT(world->Get<TransformComponent>(crate).Position.Z > 3.5f);

	// Taking the controller away removes the character and ends its contacts.
	events.Clear();
	world->Remove<CharacterControllerComponent>(player);
	RunCharacters(*world.Get(), physics, PhysicsWorld::FixedStep, &events);
	FP_EXPECT(physics.GetCharacterCount() == 0);
	FP_EXPECT(Saw(events, PhysicsEvent::Kind::CollisionExit, player, floor));
}

#include "Engine/Physics/PlayerInputSystem.h"
#include "Engine/Input.h"

FP_TEST(Physics_PlayerInputWalksAndLooks)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	MakeFloor(*world.Get());
	const Entity player = world->CreateEntity("Player");
	world->Get<TransformComponent>(player).Position = { 0, 1, 0 };
	world->Add<CharacterControllerComponent>(player);
	world->Add<PlayerInputComponent>(player);
	const Entity camera = world->CreateEntity("Player Camera");
	world->SetParent(camera, player);
	world->AddSystem<PlayerInputSystem>();
	world->AddSystem<PhysicsSystem>();
	world->BeginPlay();
	Run(*world.Get(), 0.5f);

	// W walks forward, which is -Z before any turning.
	Input::SetKey(SDL_SCANCODE_W, true);
	Run(*world.Get(), 1.0f);
	Input::SetKey(SDL_SCANCODE_W, false);
	FP_EXPECT(world->Get<TransformComponent>(player).Position.Z < -3);

	// The mouse turns the player and tilts its camera.
	Input::AddMouseDelta({ 100, 50 });
	world->Update(PhysicsWorld::FixedStep);
	Input::EndFrame();
	FP_EXPECT(world->Get<TransformComponent>(player).Rotation.Y < -10);
	FP_EXPECT(world->Get<TransformComponent>(camera).Rotation.X < -5);
	world->EndPlay();
}
