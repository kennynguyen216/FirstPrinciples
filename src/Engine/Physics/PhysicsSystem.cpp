#include "Engine/Physics/PhysicsSystem.h"
#include "Engine/Physics/PhysicsWorld.h"

namespace FP
{
	void PhysicsSystem::OnStart(World& world)
	{
		static_cast<PhysicsWorld&>(world.GetPhysics()).Sync(world);
	}

	void PhysicsSystem::Update(World& world, float deltaTime)
	{
		PhysicsWorld& physics = static_cast<PhysicsWorld&>(world.GetPhysics());
		physics.Sync(world);
		if (physics.Step(deltaTime) > 0)
			physics.WriteBack(world);
	}

	void PhysicsSystem::OnStop(World& world)
	{
		static_cast<PhysicsWorld&>(world.GetPhysics()).Clear();
	}
}
