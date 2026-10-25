#include "Engine/Physics/PhysicsSystem.h"
#include "Engine/Physics/PhysicsWorld.h"
#include "Engine/Scripting/Script.h"

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
		{
			physics.WriteBack(world);
			physics.WriteCharacters(world);
		}

		Array<PhysicsEvent> events;
		physics.TakeEvents(events);
		for (const PhysicsEvent& event : events)
		{
			// Both sides hear it, each with the normal pointing away from the other.
			Notify(world, event, event.A, event.B, event.Normal * -1.0f);
			Notify(world, event, event.B, event.A, event.Normal);
		}
	}

	void PhysicsSystem::Notify(World& world, const PhysicsEvent& event, Entity self, Entity other, Vec3 normal)
	{
		const ScriptComponent* scripts = world.IsValid(self) ? world.TryGet<ScriptComponent>(self) : nullptr;
		if (!scripts)
			return;
		// Copied first, since a script may destroy entities or add scripts while handling it.
		Array<Script*> listeners;
		for (const ScriptSlot& slot : scripts->Scripts)
		{
			if (slot.Instance)
				listeners.Add(slot.Instance);
		}
		const Collision collision{ event.Point, normal };
		for (Script* script : listeners)
		{
			switch (event.Type)
			{
				case PhysicsEvent::Kind::CollisionEnter: script->OnCollisionEnter(other, collision); break;
				case PhysicsEvent::Kind::CollisionExit: script->OnCollisionExit(other); break;
				case PhysicsEvent::Kind::TriggerEnter: script->OnTriggerEnter(other); break;
				case PhysicsEvent::Kind::TriggerExit: script->OnTriggerExit(other); break;
			}
		}
	}

	void PhysicsSystem::OnStop(World& world)
	{
		static_cast<PhysicsWorld&>(world.GetPhysics()).Clear();
	}
}
