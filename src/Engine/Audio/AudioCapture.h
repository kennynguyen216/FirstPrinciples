#pragma once

#include "Engine/Audio/AudioTypes.h"

#include <memory>

namespace FP
{
	// Explicit opt-in microphone capture. Main-thread reads return mono float PCM at 48 kHz.
	class AudioCapture
	{
	public:
		AudioCapture();
		~AudioCapture();
		AudioCapture(const AudioCapture&) = delete;
		AudioCapture& operator=(const AudioCapture&) = delete;
		AudioResult Initialize();
		void Shutdown();
		uint32 Read(float* mono, uint32 capacity);
		bool IsReady() const;
		String GetStatusText() const;
		static constexpr int SampleRate = 48000;

	private:
		friend bool AudioCaptureOfflineSelfCheck();
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}
