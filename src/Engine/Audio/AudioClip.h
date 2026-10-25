#pragma once

#include "Engine/Audio/AudioTypes.h"

#include <memory>
#include <vector>

namespace FP
{
	struct AudioClip
	{
		String Path;
		String StreamPath; // Absolute file backing long clips; samples stay on disk.
		bool IsStreaming() const { return !StreamPath.IsEmpty(); }
		uint64 Revision = 0;
		int SampleRate = 0;
		int Channels = 0;
		uint64 Frames = 0;
		double Duration = 0;
		String Encoding;
		std::vector<float> Samples;
		uint64 ClippedSamples = 0;
		uint64 SourceBytes = 0;
		int64 LastWriteTicks = 0;
	};

	struct AudioClipResult
	{
		AudioResult Result;
		std::shared_ptr<const AudioClip> Clip;
		explicit operator bool() const { return static_cast<bool>(Result) && Clip != nullptr; }
	};

	class AudioClipCache
	{
	public:
		explicit AudioClipCache(const String& contentRoot = {}, uint64 residentBudget = 128ull * 1024 * 1024,
			uint64 transientBudget = 128ull * 1024 * 1024);
		~AudioClipCache();
		AudioClipCache(const AudioClipCache&) = delete;
		AudioClipCache& operator=(const AudioClipCache&) = delete;
		// Spatial playback requests a cached mono mix; the source file stays unchanged.
		AudioClipResult Load(const String& path, bool reload = false, bool mono = false);
		static AudioClipResult ValidateFile(const String& absolutePath);
		void ClearFailures();
		uint64 ResidentBytes() const;
		uint64 PinnedBytes() const;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}
