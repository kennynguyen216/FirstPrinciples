#pragma once

#include "Engine/Audio/AudioClip.h"

union SDL_Event;

namespace FP
{
	class AudioDevice
	{
	public:
		AudioDevice();
		~AudioDevice();
		AudioDevice(const AudioDevice&) = delete;
		AudioDevice& operator=(const AudioDevice&) = delete;
		AudioResult Initialize();
		void Shutdown();
		bool IsReady() const;
		AudioClipCache& GetClipCache();
		AudioPlayResult Play(uint64 context, const std::shared_ptr<const AudioClip>& clip,
			const AudioPlaybackParams& params, VoiceHandle replace = {}, bool startPaused = false);
		AudioPlayResult CreateStream(uint64 context, int sampleRate, const AudioPlaybackParams& params);
		// Main-thread mono PCM input. Full/nonfinite batches fail without changing queued samples.
		bool QueueStream(VoiceHandle voice, const float* samples, uint32 count);
		bool SetParameters(VoiceHandle voice, const AudioPlaybackParams& params);
		bool Stop(VoiceHandle voice);
		bool Pause(VoiceHandle voice);
		bool Resume(VoiceHandle voice);
		VoiceState GetState(VoiceHandle voice) const;
		void StopContext(uint64 context);
		void Update();
		bool SetBusGain(AudioBus bus, float gain);
		void SetBusMuted(AudioBus bus, bool muted);
		bool SetMasterGain(float gain);
		void SetMasterMuted(bool muted);
		String GetStatusText() const;
		void ProcessEvent(const SDL_Event& event);
		std::weak_ptr<void> GetLifetimeToken() const;

	private:
		friend bool AudioBackendOfflineSelfCheck();
		friend bool AudioStreamingOfflineSelfCheck();
		AudioPlayResult StartVoice(uint64 context, const std::shared_ptr<const AudioClip>& clip, int sampleRate,
			const AudioPlaybackParams& params, VoiceHandle replace, bool startPaused);
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}
