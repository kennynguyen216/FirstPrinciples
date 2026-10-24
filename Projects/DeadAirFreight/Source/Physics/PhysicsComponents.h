#pragma once

#include "Engine/Engine.h"

// Collision data for Jolt. Gameplay only talks to it through PhysicsQueries.h, so swapping the
// fallback queries for Jolt's leaves every caller alone. If the engine ships its own collider and
// body components with Jolt, move these fields over and delete this file.

// What a collider belongs to. Mirrors Jolt's object layers: each one decides what it collides with
// and what queries can hit it.
enum class PhysicsLayer : FP::uint8
{
	Static, // asteroids, stations, bay walls
	Truck,
	Cargo, // tanks in the bay. Missed shots that land here puncture them (M3)
	Siphon, // creatures (M3)
	Player, // Defender and Driver on foot
	Trigger, // repair stations, the airlock zone, the colony's arrival zone
	Count,
};

using PhysicsLayerMask = FP::uint32;
inline constexpr PhysicsLayerMask AllPhysicsLayers = ~0u;
inline constexpr PhysicsLayerMask LayerBit(PhysicsLayer layer) { return 1u << static_cast<FP::uint32>(layer); }

// Shape in the entity's local space. Transform.Scale stretches it, the way Jolt's scaled shapes do.
struct ColliderComponent
{
	FP::String Shape = "Box"; // Box, Sphere or Capsule
	FP::Vec3 HalfExtents{ 0.5f, 0.5f, 0.5f }; // Box
	float Radius = 0.5f; // Sphere and Capsule
	float HalfHeight = 0.5f; // Capsule, along local Y, not counting the end caps
	FP::String Layer = "Static";
};

// Present on anything Jolt should simulate. Without it a collider is static.
struct RigidBodyComponent
{
	// Static never moves. Kinematic is moved by game code and pushes others. Dynamic is moved by forces.
	FP::String Motion = "Kinematic";
	float Mass = 1000; // kg
	float LinearDamping = 0;
	float AngularDamping = 0.5f;
	float GravityFactor = 0; // open space
};

PhysicsLayer ParsePhysicsLayer(const FP::String& name);
const FP::Array<const char*>& GetPhysicsLayerNames();
const FP::Array<const char*>& GetColliderShapeNames();
const FP::Array<const char*>& GetMotionTypeNames();
