#include "Audio/Sounds.h"

using namespace FP;

namespace
{
	// Positioned sounds are full volume within this many units and silent beyond the far one.
	constexpr float NearDistance = 2;
	constexpr float FarDistance = 30;

	const String* GetField(const SoundCuesComponent& cues, SoundCue cue)
	{
		const String* fields[] = { &cues.LaserFire, &cues.RepairBeam, &cues.TankPuncture, &cues.TankPatched, &cues.MiteSpawn,
			&cues.MiteChitter, &cues.MiteBite, &cues.MiteDeath, &cues.Boost, &cues.Hatch, &cues.AirlockVent,
			&cues.Countdown, &cues.Go };
		const int32 index = static_cast<int32>(cue);
		return index < static_cast<int32>(sizeof(fields) / sizeof(fields[0])) ? fields[index] : nullptr;
	}

	void PlayClip(World& world, const String& clip, const OneShotParams& params)
	{
		if (clip.IsEmpty())
			return;
		const AudioPlayResult played = world.GetAudio().PlayOneShot(clip, params);
		if (!played)
			FP_LOG_VERBOSE("Sound %s: %s", clip.CStr(), played.Result.Message.CStr());
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

	void Play(World& world, SoundCue cue)
	{
		PlayClip(world, GetClip(world, cue), {});
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
}

void SoundPreloadSystem::OnStart(World& world)
{
	for (auto [entity, cues] : world.View<SoundCuesComponent>().each())
	{
		for (int32 i = 0; i <= static_cast<int32>(SoundCue::Go); ++i)
		{
			const String* clip = GetField(cues, static_cast<SoundCue>(i));
			if (!clip || clip->IsEmpty())
				continue;
			if (const AudioResult loaded = world.GetAudio().Preload(*clip); !loaded)
				FP_LOG_WARN("Sound cue clip %s: %s", clip->CStr(), loaded.Message.CStr());
		}
	}
}
