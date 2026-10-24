#pragma once

#include "Physics/PhysicsComponents.h"

// The questions gameplay asks the physics world. Laser shots raycast, the airlock and repair
// stations overlap.
//
// PhysicsQueries.cpp answers them by testing every collider, which is fine for the few dozen
// tanks and creatures in a bay. When Jolt lands, answer them with its NarrowPhaseQuery (CastRay,
// CollideShape) instead and keep these signatures, so no gameplay code changes.
namespace PhysicsQueries
{
	struct RayHit
	{
		FP::Entity Entity = FP::NullEntity;
		FP::Vec3 Point;
		FP::Vec3 Normal;
		float Distance = 0;
	};

	// The closest collider on the mask along a ray. Direction need not be normalized.
	bool Raycast(FP::World& world, FP::Vec3 origin, FP::Vec3 direction, float maxDistance, PhysicsLayerMask mask, RayHit& outHit);

	// Every collider on the mask touching a sphere.
	FP::Array<FP::Entity> OverlapSphere(FP::World& world, FP::Vec3 center, float radius, PhysicsLayerMask mask);
}
