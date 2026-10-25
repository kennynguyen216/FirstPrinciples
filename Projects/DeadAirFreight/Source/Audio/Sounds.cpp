#include "Audio/Sounds.h"

using namespace FP;

namespace
{
	// Positioned sounds are full volume within this many units and silent beyond the far one.
	constexpr float NearDistance = 2;
	constexpr float FarDistance = 30;

	// The entity that carries a LoopSlot's sound.
	struct LoopVoice
	{
		LoopSlot Slot = LoopSlot::Footsteps;
	};

	const String* GetField(const SoundCuesComponent& cues, SoundCue cue)
	{
		const String* fields[] = { &cues.LaserFire, &cues.RepairBeam, &cues.TankPuncture, &cues.TankPatched, &cues.MiteSpawn,
			&cues.MiteChitter, &cues.MiteBite, &cues.MiteDeath, &cues.Boost, &cues.Hatch, &cues.AirlockVent,
			&cues.Countdown, &cues.Go, &cues.JackpotBoost, &cues.Walk, &cues.Run,
			&cues.Engine, &cues.Scratch, &cues.TicketPull, &cues.TicketWin, &cues.TicketDud, &cues.Steer, &cues.SteerStart, &cues.SteerStop };
		const int32 index = static_cast<int32>(cue);
		return index < static_cast<int32>(sizeof(fields) / sizeof(fields[0])) ? fields[index] : nullptr;
	}

	VoiceHandle PlayClip(World& world, const String& clip, const OneShotParams& params)
	{
		if (clip.IsEmpty())
			return {};
		const AudioPlayResult played = world.GetAudio().PlayOneShot(clip, params);
		if (!played)
			FP_LOG_VERBOSE("Sound %s: %s", clip.CStr(), played.Result.Message.CStr());
		return played.Voice;
	}
}

namespace Sounds
{
	String GetClip(World& world, SoundCue cue)
	{
		for (auto [entity, cues] : world.View<SoundCuesComponent>().each())
		{
			if (const String* clip = GetField(cues, cue))
				return *clip;
		}
		return {};
	}

	VoiceHandle Play(World& world, SoundCue cue, float volume)
	{
		OneShotParams params;
		params.Volume = volume;
		return PlayClip(world, GetClip(world, cue), params);
	}

	void PlayAt(World& world, SoundCue cue, Vec3 position)
	{
		OneShotParams params;
		params.Spatial = true;
		params.Position = position;
		params.MinDistance = NearDistance;
		params.MaxDistance = FarDistance;
		PlayClip(world, GetClip(world, cue), params);
	}

	void SetLoop(World& world, LoopSlot slot, SoundCue cue, bool on)
	{
		const String clip = on ? GetClip(world, cue) : String{};
		Entity voice = NullEntity;
		for (auto [entity, loop] : world.View<LoopVoice>().each())
		{
			if (loop.Slot == slot)
				voice = entity;
		}
		if (voice == NullEntity)
		{
			if (clip.IsEmpty())
				return;
			const char* names[] = { "Footsteps", "Engine Loop", "Scratch Loop", "Steer Loop" };
			voice = world.CreateEntity(names[static_cast<int32>(slot)]);
			world.Add<LoopVoice>(voice).Slot = slot;
			world.Add<AudioSourceComponent>(voice).Loop = true;
		}
		AudioSourceComponent& source = world.Get<AudioSourceComponent>(voice);
		if (source.Clip == clip)
			return;
		world.GetAudio().StopSource(voice);
		source.Clip = clip;
		if (!clip.IsEmpty())
			world.GetAudio().Play(voice);
	}
}

void SoundPreloadSystem::OnStart(World& world)
{
	for (auto [entity, cues] : world.View<SoundCuesComponent>().each())
	{
		for (int32 i = 0; i <= static_cast<int32>(SoundCue::SteerStop); ++i)
		{
			const String* clip = GetField(cues, static_cast<SoundCue>(i));
			if (!clip || clip->IsEmpty())
				continue;
			if (const AudioResult loaded = world.GetAudio().Preload(*clip); !loaded)
				FP_LOG_WARN("Sound cue clip %s: %s", clip->CStr(), loaded.Message.CStr());
		}
	}
}
