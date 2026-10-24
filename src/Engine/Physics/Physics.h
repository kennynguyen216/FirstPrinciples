#pragma once

#include "Core/CoreMinimal.h"
#include "Engine/Math.h"

#include <entt/entity/entity.hpp>

namespace FP
{
	struct RaycastHit
	{
		entt::entity Entity = entt::null;
		Vec3 Point;
		Vec3 Normal;     // facing back toward the ray
		float Distance = 0;
	};

	// Where two bodies touched, from the point of view of the one being told.
	struct Collision
	{
		Vec3 Point;
		Vec3 Normal; // pointing away from the other body
	};

	// The physics of one world. Scripts get it with GetPhysics(). Every call runs in the engine,
	// so nothing here keeps code from a script module that may reload. Forces and impulses only
	// move dynamic bodies, and only bodies that exist, which is from the first physics step after
	// play starts or the collider is added.
	class Physics
	{
	public:
		virtual ~Physics() = default;

		// A push that lasts one step, in newtons. Call it every frame for a steady push.
		virtual void AddForce(entt::entity entity, Vec3 force) = 0;
		// An instant kick, in newton seconds. Mass decides how much it changes the velocity.
		virtual void AddImpulse(entt::entity entity, Vec3 impulse) = 0;
		virtual void AddTorque(entt::entity entity, Vec3 torque) = 0;

		// Like AddForce and AddImpulse but at a world point, so an off center push also spins.
		virtual void AddForceAtPoint(entt::entity entity, Vec3 force, Vec3 point) = 0;
		virtual void AddImpulseAtPoint(entt::entity entity, Vec3 impulse, Vec3 point) = 0;

		// Kicks every dynamic body near center away from it. The kick is impulse at the center and
		// fades to nothing at radius.
		virtual void AddExplosion(Vec3 center, float radius, float impulse) = 0;

		// In kilograms. Zero for bodies that do not move and entities without one.
		virtual float GetMass(entt::entity entity) const = 0;

		virtual Vec3 GetVelocity(entt::entity entity) const = 0;
		virtual void SetVelocity(entt::entity entity, Vec3 velocity) = 0;
		virtual Vec3 GetAngularVelocity(entt::entity entity) const = 0;
		virtual void SetAngularVelocity(entt::entity entity, Vec3 velocity) = 0;

		// The closest body along a ray, skipping ignore and triggers. direction need not be unit length.
		virtual bool Raycast(Vec3 origin, Vec3 direction, float maxDistance, RaycastHit& hit, entt::entity ignore = entt::null) const = 0;

		// Like Raycast but sweeps a ball, which is more forgiving for aiming and checking for room.
		virtual bool SphereCast(Vec3 origin, float radius, Vec3 direction, float maxDistance, RaycastHit& hit, entt::entity ignore = entt::null) const = 0;

		// Every entity with a body touching the sphere, triggers included.
		virtual Array<entt::entity> OverlapSphere(Vec3 center, float radius) const = 0;

		virtual Vec3 GetGravity() const = 0;
		virtual void SetGravity(Vec3 gravity) = 0;
	};
}
