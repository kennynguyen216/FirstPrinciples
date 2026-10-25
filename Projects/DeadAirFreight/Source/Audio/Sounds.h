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
	FP::String Hatch; // climbing between the cab and the cargo bay
	FP::String AirlockVent;
	FP::String Countdown;
	FP::String Go;
	FP::String JackpotBoost; // layered over Boost when a ticket wins the jackpot
	FP::String Walk; // loops while the local player walks
	FP::String Run; // loops while the local player sprints
	FP::String Engine; // loops while the truck cruises, silent while the boost sound plays
	FP::String Scratch; // loops while the coin scratches a ticket
	FP::String TicketPull; // pulling out a new scratch ticket
	FP::String TicketWin; // a ticket reveals a minor prize
	FP::String TicketDud; // a ticket reveals a dud
	FP::String Steer; // loops while the local driver steers
	FP::String SteerStart; // ramps up into Steer when steering starts
	FP::String SteerStop; // winds Steer down when steering stops
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
	Hatch,
	AirlockVent,
	Countdown,
	Go,
	JackpotBoost,
	Walk,
	Run,
	Engine,
	Scratch,
	TicketPull,
	TicketWin,
	TicketDud,
	Steer,
	SteerStart,
	SteerStop,
};

// A looping, unpositioned sound. Each slot plays one clip at a time.
enum class LoopSlot : FP::uint8
{
	Footsteps,
	Engine,
	Scratch,
	Steer,
};

namespace Sounds
{
	// The clip for a cue, or an empty string when none is set.
	FP::String GetClip(FP::World& world, SoundCue cue);

	// Plays a cue once and returns its voice. Positioned sounds fade with distance from the camera.
	FP::VoiceHandle Play(FP::World& world, SoundCue cue, float volume = 1);
	void PlayAt(FP::World& world, SoundCue cue, FP::Vec3 position);

	// Loops a cue in a slot, switching clip when the cue changes. Off stops the slot.
	void SetLoop(FP::World& world, LoopSlot slot, SoundCue cue, bool on);
}

// Loads every set clip when play starts so the first time a cue plays has no hitch.
class SoundPreloadSystem : public FP::System
{
public:
	const char* GetName() const override { return "SoundPreload"; }
	void OnStart(FP::World& world) override;
	void Update(FP::World&, float) override {}
};
