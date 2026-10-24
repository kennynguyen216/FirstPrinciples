#pragma once

#include "Engine/Engine.h"

// Which clip plays for each game event, on the Match entity. Paths are WAVs under Content, such as
// "Audio/LaserFire.wav". A cue left empty is silent, so clips can be added one at a time.
struct SoundCuesComponent
{
	FP::String LaserFire;
	FP::String RepairBeam; // plays repeatedly while a repair beam is held
	FP::String TankPuncture;
	FP::String TankPatched;
	FP::String MiteSpawn;
	FP::String MiteChitter; // loops on every awake Mite, positioned, so the Defender can hear where they are
	FP::String MiteBite; // a Mite chews a hole
	FP::String MiteDeath;
	FP::String Boost;
	FP::String Countdown;
	FP::String Go;
};

enum class SoundCue : FP::uint8
{
	LaserFire,
	RepairBeam,
	TankPuncture,
	TankPatched,
	MiteSpawn,
	MiteChitter,
	MiteBite,
	MiteDeath,
	Boost,
	Countdown,
	Go,
};

namespace Sounds
{
	// The clip for a cue, or an empty string when none is set.
	FP::String GetClip(FP::World& world, SoundCue cue);

	// Plays a cue once. Positioned sounds fade with distance from the camera.
	void Play(FP::World& world, SoundCue cue);
	void PlayAt(FP::World& world, SoundCue cue, FP::Vec3 position);
}

// Loads every set clip when play starts so the first time a cue plays has no hitch.
class SoundPreloadSystem : public FP::System
{
public:
	const char* GetName() const override { return "SoundPreload"; }
	void OnStart(FP::World& world) override;
	void Update(FP::World&, float) override {}
};
