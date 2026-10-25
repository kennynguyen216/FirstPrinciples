#pragma once

#include "Engine/Engine.h"

// Walking and the mining laser for the Defender in the bay. On the Match entity.
struct OnFootConfigComponent
{
	float MoveSpeed = 6;
	float SprintScale = 1.8f; // move speed times this while holding Shift
	float LookSensitivity = 0.15f; // degrees per pixel the mouse moves
	float EyeHeight = 2.4f; // the same as the cab's, so the view stays level through the hatch
	float BodyRadius = 0.5f; // keeps walkers, and the camera at their eyes, out of tanks and walls
	float LaserRange = 60;
	// Vaporize mode.
	float FireInterval = 0.15f; // seconds between shots while the left button is held
	float LaserDamage = 1;
	float BeamSeconds = 0.06f; // how long a shot's beam stays visible
	// Holding the trigger heats the gun: the beam fattens, sparks fly harder and the view shakes more
	// and more. It takes this long to get fully hot, and this long to cool after letting go.
	float HeatUpSeconds = 2.5f;
	float HeatDownSeconds = 1.0f;
	float ShakeDegrees = 1.2f; // the view's wander at full heat
	float ShakeRollDegrees = 0.8f;
	float RecoilDegrees = 0.35f; // each shot's kick
	// Shaking at full heat this long overheats the gun, which then cannot fire for a while.
	float OverheatAfterSeconds = 3;
	float OverheatSeconds = 1.5f;
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
	float Heat = 0; // 0 cold to 1 fully hot, from holding the trigger
	float Kick = 0; // the latest shot's recoil, dying away
	float MaxedFor = 0; // seconds fired at full heat without a break
	float OverheatLeft = 0; // seconds until an overheated gun fires again
	float Time = 0; // drives the shake and the beam's flicker
	FP::Entity Beam = FP::NullEntity;
	FP::Entity Crosshair = FP::NullEntity;
	FP::Entity Embers = FP::NullEntity; // off the hot gun
	FP::Entity Sparkle = FP::NullEntity; // where the repair beam touches
	FP::Entity Steam = FP::NullEntity; // venting from an overheated gun
};
