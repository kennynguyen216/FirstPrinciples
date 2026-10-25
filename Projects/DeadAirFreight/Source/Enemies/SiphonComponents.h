#pragma once

#include "Engine/Engine.h"

// Creature tuning, on the Match entity.
struct SiphonConfigComponent
{
	// Mites made per crew when play starts and reused after. Nothing is created or destroyed while racing.
	FP::int32 MitePoolPerCrew = 24;
	float MiteHealth = 4;
	// How much bigger a Mite swells as it takes damage, at the brink of popping.
	float MiteHitSwell = 0.7f;
	float MiteSpeed = 4;
	float MiteDrainKgPerSecond = 0.5f;
	// Most oxygen a Mite holds. A full one keeps its grip but stops feeding.
	float MiteBellyKg = 4;
	// Seconds latched on before a Mite chews a hole in its tank. 0 never does.
	float MiteBiteHoleSeconds = 4;
	// A Mite gets into each bay after a random wait between these while racing. Both bays get theirs
	// at the same moments.
	float MiteTrickleSeconds = 12;
	float MiteTrickleMaxSeconds = 16;
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
	// Its place in the crew's pool. Every machine fills pools in the same order, so the network can
	// name a creature by crew and index.
	FP::int32 Index = 0;
	SiphonKind Kind = SiphonKind::Mite;
	bool Active = false;
	float Health = 0;
	// Oxygen it has eaten. Returned to the bay when it dies, lost if it is flushed out the airlock (M5).
	float StolenOxygenKg = 0;
	FP::Entity Target = FP::NullEntity; // the tank it is after
	float LatchedSeconds = 0;
	bool Bitten = false; // already chewed a hole in this tank
	float Age = 0;

	// What every machine needs to draw it, kept by the host and sent with the bay. Size grows with
	// its belly and with every hit it takes, like a water balloon filling. The counters only go up,
	// so a machine that sees one change knows the Mite was hit, or popped, since it last looked.
	float Size = 0.7f;
	FP::uint8 Hits = 0;
	FP::uint8 Pops = 0;
};
