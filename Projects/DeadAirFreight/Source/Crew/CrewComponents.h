#pragma once

#include "Engine/Engine.h"

// Walking and the mining laser for the Defender in the bay. On the Match entity.
struct OnFootConfigComponent
{
	float MoveSpeed = 6;
	float LookSensitivity = 0.2f; // degrees per pixel while the right mouse button is held
	float EyeHeight = 1.6f;
	float BodyRadius = 0.4f; // keeps walkers out of tanks and walls
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
	FP::Vec2 LastMouse;
};
