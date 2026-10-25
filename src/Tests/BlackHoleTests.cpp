#include "Tests/TestFramework.h"
#include "Engine/Engine.h"
#include "Engine/Physics/BlackHoleSystem.h"
#include "Engine/Physics/PhysicsSystem.h"
#include "Engine/Physics/PhysicsWorld.h"

#include <cmath>

using namespace FP;

namespace
{
	constexpr float Rs = 1;
	constexpr float C = 20;

	// Runs a body around a hole at the origin. Returns the hole it fell into, or -1.
	int32 Fly(Vec3& position, Vec3& velocity, float seconds, Vec3 holeVelocity = {})
	{
		BlackHole::Field hole{ {}, holeVelocity, Rs, C };
		for (float t = 0; t < seconds; t += 1.0f / 60)
		{
			if (BlackHole::Step(position, velocity, {}, &hole, 1, 1.0f / 60) >= 0)
				return 0;
		}
		return -1;
	}

	bool OrbitSurvives(float radius, float speedScale, float seconds)
	{
		Vec3 position{ radius * Rs, 0, 0 };
		Vec3 velocity{ 0, BlackHole::CircularSpeed(radius * Rs, Rs, C) * speedScale, 0 };
		return Fly(position, velocity, seconds) < 0;
	}
}

FP_TEST(BlackHole_CircularOrbitHoldsRadius)
{
	Vec3 position{ 10 * Rs, 0, 0 };
	Vec3 velocity{ 0, BlackHole::CircularSpeed(10 * Rs, Rs, C), 0 };
	FP_EXPECT(Fly(position, velocity, 5) < 0);
	FP_EXPECT(std::fabs(position.Length() - 10 * Rs) < 0.01f * Rs);
}

FP_TEST(BlackHole_InnermostStableOrbitIsThreeRs)
{
	// A 1% nudge stays bound outside 3 Rs and plunges inside it, as in general relativity.
	FP_EXPECT(OrbitSurvives(4, 0.99f, 10));
	FP_EXPECT(!OrbitSurvives(2.7f, 0.99f, 10));
}

FP_TEST(BlackHole_StaticFlybyKeepsSpeed)
{
	// A still hole bends the path but hands back the same speed. Only a moving hole adds speed.
	Vec3 position{ -60, 6, 0 };
	Vec3 velocity{ 12, 0, 0 };
	FP_EXPECT(Fly(position, velocity, 10) < 0);
	FP_EXPECT(velocity.Y < -1); // deflected toward the hole
	FP_EXPECT(std::fabs(velocity.Length() - 12) < 0.12f);

	Vec3 assisted{ -60, 6, 0 };
	Vec3 assistedVelocity{ 12, 0, 0 };
	FP_EXPECT(Fly(assisted, assistedVelocity, 10, { -4, 0, 0 }) < 0);
	FP_EXPECT(assistedVelocity.Length() > 12.5f);
}

FP_TEST(BlackHole_FallingInIsCaptured)
{
	Vec3 position{ 8 * Rs, 0, 0 };
	Vec3 velocity;
	FP_EXPECT(Fly(position, velocity, 5) == 0);
}

FP_TEST(BlackHole_PullsBodiesInWorld)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	const Entity hole = world->CreateEntity("Hole");
	world->Add<BlackHoleComponent>(hole).SchwarzschildRadius = 1;

	// One body moves itself, the other goes through Jolt with world gravity off.
	const Entity free = world->CreateEntity("Free");
	world->Get<TransformComponent>(free).Position = { 6, 0, 0 };
	world->Add<GravityBodyComponent>(free);
	const Entity rigid = world->CreateEntity("Rigid");
	world->Get<TransformComponent>(rigid).Position = { -6, 0, 0 };
	world->Add<GravityBodyComponent>(rigid);
	world->Add<ColliderComponent>(rigid).Shape = "Sphere";
	world->Add<RigidBodyComponent>(rigid).GravityScale = 0;

	world->AddSystem<BlackHoleSystem>();
	world->AddSystem<PhysicsSystem>();
	world->BeginPlay();
	world->Update(PhysicsWorld::FixedStep);
	FP_EXPECT(world->Get<TransformComponent>(free).Position.X < 6);
	FP_EXPECT(world->GetPhysics().GetVelocity(rigid).X > 0);

	// Falling from rest at 6 Rs takes well under a second at c = 20.
	for (int i = 0; i < 120; ++i)
		world->Update(PhysicsWorld::FixedStep);
	FP_EXPECT(world->Get<GravityBodyComponent>(free).Captured);
	FP_EXPECT(world->Get<GravityBodyComponent>(rigid).Captured);
}
