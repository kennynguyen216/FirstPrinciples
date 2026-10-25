#pragma once

#include "Core/CoreMinimal.h"
#include "Engine/Math.h"

#include <entt/entity/entity.hpp>
#include <limits>
#include <array>

namespace FP
{
	uint64 AllocateAudioContext();
	inline float AudioDampingCutoff(float damping)
	{
		return damping > 0 ? float(20000.0 * std::pow(0.0036, double(damping))) : 0.f;
	}

	enum class AudioError : uint8
	{
		Ok, NotReady, InvalidSource, InvalidSettings, MissingClip,
		UnsupportedFormat, DecodeFailed, BudgetExceeded, VoiceLimit, BackendFailure
	};

	struct AudioResult
	{
		AudioError Code = AudioError::Ok;
		String Message;
		explicit operator bool() const { return Code == AudioError::Ok; }
	};

	enum class AudioBus : uint8 { SFX, Music, UI, Voice };
	enum class VoiceState : uint8 { Invalid, Playing, Paused, Draining };

	struct VoiceHandle
	{
		uint64 Context = 0;
		uint64 Generation = 0;
		uint32 Slot = std::numeric_limits<uint32>::max();
		explicit operator bool() const { return Context != 0 && Generation != 0 && Slot != std::numeric_limits<uint32>::max(); }
		bool operator==(const VoiceHandle&) const = default;
	};

	struct AudioPlayResult
	{
		VoiceHandle Voice;
		AudioResult Result;
		explicit operator bool() const { return static_cast<bool>(Result) && static_cast<bool>(Voice); }
	};

	struct AudioReflection
	{
		float Delay = 0; // seconds after the direct sound
		float LeftGain = 0, RightGain = 0;
		float Damping = 0;
	};

	struct AudioRoomParams
	{
		float Wet = 0, Decay = 0.5f, Damping = 0.5f, LateGain = 0;
		std::array<AudioReflection, 6> Reflections{};
	};

	struct AudioPlaybackParams
	{
		float Volume = 1;
		float Pitch = 1;
		bool Loop = false;
		AudioBus Bus = AudioBus::SFX;
		int32 Priority = 128;
		float LeftGain = 1;
		float RightGain = 1;
		bool Preview = false;
		float LowPassHz = 0; // zero bypasses the filter
		AudioRoomParams Room;
	};

	struct OneShotParams
	{
		float Volume = 1;
		float Pitch = 1;
		AudioBus Bus = AudioBus::SFX;
		int32 Priority = 128;
		bool Spatial = false;
		entt::entity Owner = entt::null;
		Vec3 Position;
		float MinDistance = 1;
		float MaxDistance = 25;
		bool Occlusion = true;
		float DopplerScale = 1;
	};
}
