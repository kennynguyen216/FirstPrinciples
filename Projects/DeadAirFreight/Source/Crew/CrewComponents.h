#pragma once

#include "Engine/Engine.h"

// Walking and the mining laser for the Defender in the bay. On the Match entity.
struct OnFootConfigComponent
{
	float MoveSpeed = 6;
	float LookSensitivity = 0.15f; // degrees per pixel the mouse moves
	float EyeHeight = 2.4f; // the same as the cab's, so the view stays level through the hatch
	float BodyRadius = 0.5f; // keeps walkers, and the camera at their eyes, out of tanks and walls
	float LaserRange = 60;
	// Vaporize mode.
	float FireInterval = 0.15f; // seconds between shots while the left button is held
	float LaserDamage = 1;
	float BeamSeconds = 0.06f; // how long a shot's beam stays visible
	// Repair mode: hold the beam on a tank to patch it.
	float PatchSecondsPerHole = 1.5f;
};

enum class LaserMode : FP::uint8
{
	Vaporize,
	Repair,
};

// A player's body while they walk, until the real models are in. Tall enough to hold the eyes, which
// sit at the cab's eye height so walking through the hatch keeps the view level.
inline const FP::Vec3 WalkerSize{ 0.8f, 2.6f, 0.8f };

// The bay end of the hatch between a crew's cab and its cargo bay. The cab end is the Driver scene's
// "CabHatch" entity, which rides along with the cab. Both are open doorways: the transform sits on the
// floor at the outside face of the back wall, and its scale is the doorway's size.
struct BayHatchComponent
{
	FP::int32 Crew = 0;
};

// On the local player's walking body while they are on foot. Added while playing.
struct OnFootComponent
{
	FP::int32 Crew = 0;
	FP::String Interior = "Bay"; // the room they walk in
	LaserMode Mode = LaserMode::Vaporize;
	float Cooldown = 0;
	float BeamTimeLeft = 0;
	float RepairSoundIn = 0;
	FP::Entity Beam = FP::NullEntity;
	FP::Entity Crosshair = FP::NullEntity;
};
