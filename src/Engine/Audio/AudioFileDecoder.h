#pragma once

#include "Engine/Audio/AudioClip.h"

namespace FP
{
	// Bounded file decoder. Owned by one thread; never used by the audio callback.
	class AudioFileDecoder
	{
	public:
		AudioFileDecoder();
		~AudioFileDecoder();
		AudioResult Open(const String& absolutePath, bool mono = false);
		const AudioClip& Info() const;
		uint64 Read(float* samples, uint64 frames);
		bool Rewind();
		const AudioResult& Result() const;
	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}
