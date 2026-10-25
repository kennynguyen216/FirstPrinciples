#include "Engine/Audio/AudioCapture.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <thread>

namespace FP
{
	struct AudioCapture::Impl
	{
		SDL_AudioStream* Stream = nullptr;
		bool Initialized = false;
		std::atomic<bool> Failed{false};
		std::array<float, SampleRate / 4> Ring{};
		std::array<float, 512> Scratch{};
		size_t Head = 0, Count = 0;
		uint64 Dropped = 0;
		String Error;
		std::thread::id Thread = std::this_thread::get_id();
		void CheckThread() const { FP_ASSERT(Thread == std::this_thread::get_id()); }
		void Store(const float* samples, uint32 count)
		{
			for (uint32 i = 0; i < count; ++i)
			{
				if (Count == Ring.size()) { Head = (Head + 1) % Ring.size(); --Count; ++Dropped; }
				Ring[(Head + Count) % Ring.size()] = std::isfinite(samples[i]) ? std::clamp(samples[i], -1.f, 1.f) : 0;
				++Count;
			}
		}
		uint32 Take(float* output, uint32 capacity)
		{
			const uint32 count = static_cast<uint32>(std::min<size_t>(Count, capacity));
			for (uint32 i = 0; i < count; ++i) output[i] = Ring[(Head + i) % Ring.size()];
			Head = (Head + count) % Ring.size();
			Count -= count;
			return count;
		}
		static void SDLCALL Capture(void* userdata, SDL_AudioStream* stream, int, int)
		{
			auto& self = *static_cast<Impl*>(userdata);
			if (self.Failed.load(std::memory_order_relaxed)) { SDL_ClearAudioStream(stream); return; }
			uint32 remaining = 16384;
			while (remaining)
			{
				const int available = SDL_GetAudioStreamAvailable(stream);
				if (available < 0) { self.Failed = true; return; }
				const uint32 count = std::min({uint32(available / sizeof(float)), uint32(self.Scratch.size()), remaining});
				if (!count) return;
				const int read = SDL_GetAudioStreamData(stream, self.Scratch.data(), int(count * sizeof(float)));
				if (read < 0 || read % sizeof(float)) { self.Failed = true; return; }
				if (!read) return;
				const uint32 samples = uint32(read / sizeof(float));
				self.Store(self.Scratch.data(), samples);
				remaining -= samples;
			}
			const int stale = SDL_GetAudioStreamAvailable(stream);
			if (stale > 0) { self.Dropped += uint64(stale / sizeof(float)); SDL_ClearAudioStream(stream); }
		}
	};

	AudioCapture::AudioCapture() : m_Impl(std::make_unique<Impl>()) {}
	AudioCapture::~AudioCapture() { Shutdown(); }
	AudioResult AudioCapture::Initialize()
	{
		m_Impl->CheckThread();
		if (IsReady()) return {};
		Shutdown();
		m_Impl->Error.Clear();
		m_Impl->Failed = false;
		if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) { m_Impl->Error = SDL_GetError(); return {AudioError::NotReady, m_Impl->Error}; }
		m_Impl->Initialized = true;
		SDL_AudioSpec spec{SDL_AUDIO_F32, 1, SampleRate};
		m_Impl->Stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &spec, Impl::Capture, m_Impl.get());
		if (!m_Impl->Stream || !SDL_ResumeAudioStreamDevice(m_Impl->Stream))
		{
			m_Impl->Error = SDL_GetError();
			Shutdown();
			return {AudioError::NotReady, m_Impl->Error};
		}
		return {};
	}
	void AudioCapture::Shutdown()
	{
		m_Impl->CheckThread();
		if (m_Impl->Stream)
		{
			SDL_SetAudioStreamPutCallback(m_Impl->Stream, nullptr, nullptr);
			SDL_DestroyAudioStream(m_Impl->Stream);
			m_Impl->Stream = nullptr;
		}
		m_Impl->Head = m_Impl->Count = 0;
		if (m_Impl->Initialized) { SDL_QuitSubSystem(SDL_INIT_AUDIO); m_Impl->Initialized = false; }
	}
	bool AudioCapture::IsReady() const { return m_Impl->Stream && !m_Impl->Failed.load(); }
	uint32 AudioCapture::Read(float* mono, uint32 capacity)
	{
		m_Impl->CheckThread();
		if (!mono || !capacity || !m_Impl->Stream) return 0;
		if (m_Impl->Failed.load())
		{
			m_Impl->Error = "Microphone stream failed; disable and enable voice to retry.";
			Shutdown();
			return 0;
		}
		if (!SDL_LockAudioStream(m_Impl->Stream)) return 0;
		const uint32 count = m_Impl->Take(mono, capacity);
		SDL_UnlockAudioStream(m_Impl->Stream);
		return count;
	}
	String AudioCapture::GetStatusText() const
	{
		m_Impl->CheckThread();
		uint64 dropped = 0;
		if (m_Impl->Stream && SDL_LockAudioStream(m_Impl->Stream))
		{
			dropped = m_Impl->Dropped;
			SDL_UnlockAudioStream(m_Impl->Stream);
		}
		return String::Format("Microphone %s: 48000 Hz mono; discarded %llu stale samples%s%s", IsReady() ? "ready" : "off/unavailable",
			static_cast<unsigned long long>(dropped), m_Impl->Error.IsEmpty() ? "" : ": ", m_Impl->Error.CStr());
	}

	bool AudioCaptureOfflineSelfCheck()
	{
		AudioCapture::Impl state;
		std::array<float, 512> block{};
		for (uint32 i = 0; i < 25; ++i) { block.fill(float(i) / 25); state.Store(block.data(), uint32(block.size())); }
		if (state.Count != 12000 || state.Dropped != 800) return false;
		std::array<float, 12000> output{};
		if (state.Take(output.data(), uint32(output.size())) != 12000 || state.Count != 0) return false;
		if (std::abs(output.front() - 1.f / 25) > .00001f || std::abs(output.back() - 24.f / 25) > .00001f) return false;
		const float invalid[] = {std::numeric_limits<float>::quiet_NaN(), 2, -2};
		state.Store(invalid, 3);
		if (state.Take(output.data(), 3) != 3) return false;
		return output[0] == 0 && output[1] == 1 && output[2] == -1;
	}
}
