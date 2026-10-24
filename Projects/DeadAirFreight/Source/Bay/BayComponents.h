#pragma once

#include "Engine/Engine.h"

// The Defender's controls and laser, on the Match entity.
struct DefenderConfigComponent
{
	float MoveSpeed = 6;
	float LookSensitivity = 0.2f; // degrees per pixel while the right mouse button is held
	float EyeHeight = 1.6f;
	float BodyRadius = 0.4f; // keeps the Defender out of tanks and walls
	float FireInterval = 0.15f; // seconds between shots while the left button is held
	float LaserRange = 60;
	float LaserDamage = 1;
	float BeamSeconds = 0.06f; // how long a shot's beam stays visible
};

// Creature tuning, on the Match entity.
struct SiphonConfigComponent
{
	// Mites made per crew when play starts and reused after. Nothing is created or destroyed while racing.
	FP::int32 MitePoolPerCrew = 24;
	float MiteHealth = 2;
	float MiteSpeed = 4;
	float MiteDrainKgPerSecond = 0.5f;
	// Most oxygen a Mite holds. A full one keeps its grip but stops feeding.
	float MiteBellyKg = 4;
	// Seconds latched on before a Mite chews a hole in its tank. 0 never does.
	float MiteBiteHoleSeconds = 4;
	// A Mite gets into each bay this often while racing. Both bays get theirs at the same moments.
	float MiteTrickleSeconds = 6;
};

enum class SiphonKind : FP::uint8
{
	Mite,
	Mimic, // M8
	Borer, // M8
};

// One creature from the pool. Inactive ones wait out of sight until the spawner wakes them.
struct SiphonComponent
{
	FP::int32 Crew = 0;
	SiphonKind Kind = SiphonKind::Mite;
	bool Active = false;
	float Health = 0;
	// Oxygen it has eaten. Returned to the bay when it dies, lost if it is flushed out the airlock (M5).
	float StolenOxygenKg = 0;
	FP::Entity Target = FP::NullEntity; // the tank it is after
	float LatchedSeconds = 0;
	bool Bitten = false; // already chewed a hole in this tank
	float Age = 0;
};

// The local Defender's laser state, added while playing.
struct DefenderLaserComponent
{
	float Cooldown = 0;
	float BeamTimeLeft = 0;
	FP::Entity Beam = FP::NullEntity;
	FP::Entity Crosshair = FP::NullEntity;
	FP::Vec2 LastMouse;
};
