#pragma once

#include "Engine/Physics/Physics.h"

#include <memory>

namespace FP
{
	class World;

	// Something that happened between two bodies during a step.
	struct PhysicsEvent
	{
		enum class Kind : uint8
		{
			CollisionEnter,
			CollisionExit,
			TriggerEnter,
			TriggerExit,
		};
		Kind Type = Kind::CollisionEnter;
		entt::entity A = entt::null;
		entt::entity B = entt::null;
		Vec3 Point;
		Vec3 Normal; // from A toward B
	};

	// The engine's Physics: Jolt bodies for every entity with a collider. Host only.
	class PhysicsWorld final : public Physics
	{
	public:
		// Physics steps at a fixed rate so it behaves the same at any frame rate.
		static constexpr float FixedStep = 1.0f / 60.0f;

		PhysicsWorld();
		~PhysicsWorld() override;

		void AddForce(entt::entity entity, Vec3 force) override;
		void AddImpulse(entt::entity entity, Vec3 impulse) override;
		void AddTorque(entt::entity entity, Vec3 torque) override;
		void AddForceAtPoint(entt::entity entity, Vec3 force, Vec3 point) override;
		void AddImpulseAtPoint(entt::entity entity, Vec3 impulse, Vec3 point) override;
		void AddExplosion(Vec3 center, float radius, float impulse) override;
		float GetMass(entt::entity entity) const override;
		Vec3 GetVelocity(entt::entity entity) const override;
		void SetVelocity(entt::entity entity, Vec3 velocity) override;
		Vec3 GetAngularVelocity(entt::entity entity) const override;
		void SetAngularVelocity(entt::entity entity, Vec3 velocity) override;
		bool Raycast(Vec3 origin, Vec3 direction, float maxDistance, RaycastHit& hit,
			entt::entity ignore = entt::null, entt::entity ignoreAlso = entt::null) const override;
		Array<RaycastHit> RaycastAll(Vec3 origin, Vec3 direction, float maxDistance,
			entt::entity ignore = entt::null, entt::entity ignoreAlso = entt::null) const override;
		bool SphereCast(Vec3 origin, float radius, Vec3 direction, float maxDistance, RaycastHit& hit, entt::entity ignore) const override;
		Array<entt::entity> OverlapSphere(Vec3 center, float radius) const override;
		Vec3 GetGravity() const override;
		void SetGravity(Vec3 gravity) override;

		// Matches bodies to the world's colliders: adds new ones, rebuilds changed ones, removes
		// ones whose entity or collider is gone, and follows transforms that were moved by hand.
		void Sync(World& world);

		// Runs as many fixed steps as deltaTime covers, at most a few so a stall cannot snowball.
		// Returns how many ran.
		int32 Step(float deltaTime);

		// Copies dynamic bodies' positions and rotations back to their transforms.
		void WriteBack(World& world);

		// Takes the collision and trigger events of the steps so far.
		void TakeEvents(Array<PhysicsEvent>& out);

		// Removes every body.
		void Clear();

		int32 GetBodyCount() const;
		bool HasBody(entt::entity entity) const;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}
