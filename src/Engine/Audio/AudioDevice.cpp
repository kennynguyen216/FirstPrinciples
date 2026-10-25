#include "Engine/Audio/AudioDevice.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <thread>

namespace FP
{
	uint64 AllocateAudioContext()
	{
		static std::atomic<uint64> next{0};
		return ++next;
	}

	namespace
	{
		constexpr uint32 GameVoices = 64, PreviewSlot = GameVoices;
		bool Gain(float value) { return std::isfinite(value) && value >= 0 && value <= 1; }
		bool Valid(const AudioPlaybackParams& p)
		{
			return Gain(p.Volume) && Gain(p.LeftGain) && Gain(p.RightGain) && std::isfinite(p.Pitch) &&
				p.Pitch >= .25f && p.Pitch <= 4 && p.Priority >= 0 && p.Priority <= 255 && uint8(p.Bus) <= uint8(AudioBus::Voice) &&
				std::isfinite(p.LowPassHz) && (p.LowPassHz == 0 || (p.LowPassHz >= 20 && p.LowPassHz <= 20000));
		}
	}

	struct AudioDevice::Impl
	{
		struct Slot
		{
			SDL_AudioStream* Stream = nullptr;
			std::shared_ptr<const AudioClip> Clip;
			std::vector<float> Ring;
			size_t ReadHead = 0, Buffered = 0;
			int SampleRate = 0;
			AudioPlaybackParams Params;
			uint64 Context = 0, Generation = 0, Serial = 0, Cursor = 0;
			bool Paused = false, Ended = false, Failed = false;
			float Left = 0, Right = 0, TargetLeft = 0, TargetRight = 0;
			uint32 Ramp = 0;
			float LowPass = 1, TargetLowPass = 1, FilterLeft = 0, FilterRight = 0;
			uint32 FilterRamp = 0;
			uint64 Overruns = 0;
			std::array<float, 512 * 2> Scratch{};
		};
		std::array<std::unique_ptr<Slot>, GameVoices + 1> Voices;
		AudioClipCache Cache;
		std::shared_ptr<void> Lifetime = std::make_shared<int>(0);
		std::thread::id Thread = std::this_thread::get_id();
		SDL_AudioDeviceID Device = 0;
		bool Initialized = false, RetryPending = false, MasterMuted = false;
		float Master = 1;
		std::array<float, 4> BusGains{1, 1, 1, 1};
		std::array<bool, 4> BusMuted{};
		uint64 Generation = 0, Serial = 0, Rejected = 0, Stolen = 0, Overruns = 0;
		std::atomic<uint64> Clipped{0}, Nonfinite{0};
		std::atomic<int> OutputRate{0}, OutputChannels{0};
		String Error;

		void CheckThread() const { FP_ASSERT(Thread == std::this_thread::get_id()); }
		Slot* Find(VoiceHandle voice) const
		{
			if (!voice || voice.Slot >= Voices.size()) return nullptr;
			auto* slot = Voices[voice.Slot].get();
			return slot && slot->Context == voice.Context && slot->Generation == voice.Generation ? slot : nullptr;
		}
		void Retire(uint32 index)
		{
			auto& slot = Voices[index];
			if (!slot) return;
			SDL_SetAudioStreamGetCallback(slot->Stream, nullptr, nullptr);
			SDL_DestroyAudioStream(slot->Stream);
			Overruns += slot->Overruns;
			slot.reset();
		}
		void CloseOutput()
		{
			for (uint32 i = 0; i < Voices.size(); ++i) Retire(i);
			if (Device)
			{
				SDL_SetAudioPostmixCallback(Device, nullptr, nullptr);
				SDL_CloseAudioDevice(Device);
				Device = 0;
			}
		}
		void Targets(Slot& slot)
		{
			const int sampleRate = slot.Clip ? slot.Clip->SampleRate : slot.SampleRate;
			const size_t bus = static_cast<size_t>(slot.Params.Bus);
			const float scalar = slot.Params.Volume * (MasterMuted ? 0.f : Master) *
				(slot.Params.Preview ? 1.f : (BusMuted[bus] ? 0.f : BusGains[bus]));
			const float left = scalar * slot.Params.LeftGain, right = scalar * slot.Params.RightGain;
			if (left != slot.TargetLeft || right != slot.TargetRight)
			{
				slot.TargetLeft = left;
				slot.TargetRight = right;
				slot.Ramp = std::max(1, sampleRate / 200);
			}
			const float lowPass = slot.Params.LowPassHz == 0 ? 1.f : float(1 - std::exp(-6.283185307179586 * slot.Params.LowPassHz / sampleRate));
			if (lowPass != slot.TargetLowPass)
			{
				slot.TargetLowPass = lowPass;
				slot.FilterRamp = std::max(1, sampleRate / 200);
			}
		}
		void RefreshGains()
		{
			for (auto& slot : Voices) if (slot && SDL_LockAudioStream(slot->Stream))
			{
				Targets(*slot);
				SDL_UnlockAudioStream(slot->Stream);
			}
		}
		static void SDLCALL Fill(void* userdata, SDL_AudioStream* stream, int additional, int)
		{
			auto& slot = *static_cast<Slot*>(userdata);
			if (additional <= 0 || slot.Paused || slot.Ended || slot.Failed) return;
			uint64 requested = (uint64(additional) + sizeof(float) * 2 - 1) / (sizeof(float) * 2);
			if (requested > 16384) { ++slot.Overruns; requested = 16384; }
			while (requested)
			{
				const uint64 count = slot.Clip ? std::min({requested, uint64(512), slot.Clip->Frames - slot.Cursor}) : std::min(requested, uint64(512));
				for (uint64 frame = 0; frame < count; ++frame)
				{
					if (slot.Ramp)
					{
						slot.Left += (slot.TargetLeft - slot.Left) / slot.Ramp;
						slot.Right += (slot.TargetRight - slot.Right) / slot.Ramp;
						--slot.Ramp;
					}
					float left = 0, right = 0;
					if (slot.Clip)
					{
						const size_t offset = static_cast<size_t>((slot.Cursor + frame) * slot.Clip->Channels);
						left = slot.Clip->Samples[offset];
						right = slot.Clip->Samples[offset + (slot.Clip->Channels == 2 ? 1 : 0)];
					}
					else if (slot.Buffered)
					{
						left = right = slot.Ring[slot.ReadHead];
						slot.ReadHead = (slot.ReadHead + 1) % slot.Ring.size();
						--slot.Buffered;
					}
					if (slot.FilterRamp)
					{
						slot.LowPass += (slot.TargetLowPass - slot.LowPass) / slot.FilterRamp;
						--slot.FilterRamp;
					}
					slot.FilterLeft += slot.LowPass * (left - slot.FilterLeft);
					slot.FilterRight += slot.LowPass * (right - slot.FilterRight);
					if (std::abs(slot.FilterLeft) < 1e-20f) slot.FilterLeft = 0;
					if (std::abs(slot.FilterRight) < 1e-20f) slot.FilterRight = 0;
					slot.Scratch[frame * 2] = slot.FilterLeft * slot.Left;
					slot.Scratch[frame * 2 + 1] = slot.FilterRight * slot.Right;
				}
				if (!SDL_PutAudioStreamData(stream, slot.Scratch.data(), int(count * sizeof(float) * 2))) { slot.Failed = true; return; }
				slot.Cursor += count;
				requested -= count;
				if (slot.Clip && slot.Cursor == slot.Clip->Frames)
				{
					if (slot.Params.Loop) slot.Cursor = 0;
					else
					{
						slot.Ended = true;
						if (!SDL_FlushAudioStream(stream)) slot.Failed = true;
						return;
					}
				}
			}
		}
		static void SDLCALL Postmix(void* userdata, const SDL_AudioSpec* spec, float* buffer, int bytes)
		{
			auto& self = *static_cast<Impl*>(userdata);
			uint64 clipped = 0, nonfinite = 0;
			for (int i = 0; i < bytes / int(sizeof(float)); ++i)
			{
				if (!std::isfinite(buffer[i])) { buffer[i] = 0; ++nonfinite; }
				else if (buffer[i] < -1 || buffer[i] > 1) { buffer[i] = std::clamp(buffer[i], -1.f, 1.f); ++clipped; }
			}
			self.Clipped.fetch_add(clipped, std::memory_order_relaxed);
			self.Nonfinite.fetch_add(nonfinite, std::memory_order_relaxed);
			self.OutputRate.store(spec->freq, std::memory_order_relaxed);
			self.OutputChannels.store(spec->channels, std::memory_order_relaxed);
		}
	};

	// Kept next to callback state so offline tests can verify samples without exposing stream internals.
	bool AudioBackendOfflineSelfCheck()
	{
		SDL_AudioSpec spec{SDL_AUDIO_F32, 2, 8000};
		auto stream = std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)>(SDL_CreateAudioStream(&spec, &spec), SDL_DestroyAudioStream);
		if (!stream) return false;
		auto clip = std::make_shared<AudioClip>();
		clip->Channels = 1; clip->SampleRate = 8000; clip->Frames = 4;
		clip->Samples = {.25f, -.5f, .75f, -1.f};
		AudioDevice::Impl::Slot slot;
		slot.Clip = clip; slot.Stream = stream.get(); slot.Params.Loop = true;
		slot.Left = slot.TargetLeft = 1; slot.Right = slot.TargetRight = .5f;
		AudioDevice::Impl::Fill(&slot, stream.get(), 0, 0);
		if (slot.Cursor != 0 || SDL_GetAudioStreamQueued(stream.get()) != 0) return false;
		AudioDevice::Impl::Fill(&slot, stream.get(), 5 * 8 - 1, 0);
		std::array<float, 10> output{};
		if (SDL_GetAudioStreamData(stream.get(), output.data(), int(sizeof(output))) != sizeof(output)) return false;
		for (size_t i = 0; i < 5; ++i)
			if (std::abs(output[i * 2] - clip->Samples[i % 4]) > .00001f || std::abs(output[i * 2 + 1] - clip->Samples[i % 4] * .5f) > .00001f) return false;
		if (slot.Cursor != 1 || slot.Ended) return false;
		AudioDevice::Impl::Fill(&slot, stream.get(), std::numeric_limits<int>::max(), 0);
		if (slot.Overruns != 1 || SDL_GetAudioStreamQueued(stream.get()) != 16384 * 8) return false;
		SDL_ClearAudioStream(stream.get());
		slot.Cursor = 0; slot.Params.Loop = false;
		AudioDevice::Impl::Fill(&slot, stream.get(), 80, 0);
		if (!slot.Ended || slot.Cursor != 4) return false;
		std::array<float, 8> tail{};
		if (SDL_GetAudioStreamData(stream.get(), tail.data(), int(sizeof(tail))) != sizeof(tail)) return false;
		AudioDevice::Impl::Fill(&slot, stream.get(), 80, 0);
		if (SDL_GetAudioStreamQueued(stream.get()) != 0 || SDL_GetAudioStreamAvailable(stream.get()) != 0) return false;
		SDL_ClearAudioStream(stream.get());
		slot.Cursor = 0; slot.Ended = false; slot.Paused = true;
		AudioDevice::Impl::Fill(&slot, stream.get(), 8, 0);
		if (slot.Cursor != 0 || SDL_GetAudioStreamQueued(stream.get()) != 0) return false;
		slot.Paused = false; slot.Left = slot.Right = 0; slot.TargetLeft = slot.TargetRight = 1; slot.Ramp = 4;
		AudioDevice::Impl::Fill(&slot, stream.get(), 32, 0);
		if (SDL_GetAudioStreamData(stream.get(), tail.data(), int(sizeof(tail))) != sizeof(tail)) return false;
		for (size_t i = 0; i < 4; ++i) if (std::abs(tail[i * 2] - clip->Samples[i] * float(i + 1) / 4) > .00001f) return false;
		SDL_ClearAudioStream(stream.get());
		clip->Frames = 800; clip->Samples.assign(800, .25f);
		slot.Cursor = 0; slot.Ended = false;
		if (!SDL_SetAudioStreamFrequencyRatio(stream.get(), 2)) return false;
		AudioDevice::Impl::Fill(&slot, stream.get(), 800 * 8, 0);
		const int pitchedFrames = SDL_GetAudioStreamAvailable(stream.get()) / 8;
		if (pitchedFrames < 399 || pitchedFrames > 401 || slot.Failed) return false;
		if (const char* driver = SDL_GetHint(SDL_HINT_AUDIO_DRIVER); driver && std::string_view(driver) == "dummy")
		{
			AudioDevice device;
			if (!device.Initialize() || !SDL_PauseAudioDevice(device.m_Impl->Device)) return false;
			AudioPlaybackParams params; params.Loop = true;
			const auto voice = device.Play(1, clip, params);
			if (!voice) return false;
			auto* playing = device.m_Impl->Find(voice.Voice);
			if (!SDL_LockAudioStream(playing->Stream)) return false;
			AudioDevice::Impl::Fill(playing, playing->Stream, 80, 0);
			const int queued = SDL_GetAudioStreamQueued(playing->Stream);
			const uint64 cursor = playing->Cursor;
			SDL_UnlockAudioStream(playing->Stream);
			if (!device.Pause(voice.Voice) || SDL_GetAudioStreamQueued(playing->Stream) != queued || playing->Cursor != cursor) return false;
			if (!device.Resume(voice.Voice) || SDL_GetAudioStreamQueued(playing->Stream) != queued || playing->Cursor != cursor) return false;
			// Simulate SDL losing the logical output, then exercise the real event/retry path.
			SDL_CloseAudioDevice(device.m_Impl->Device);
			SDL_Event event{}; event.type = SDL_EVENT_AUDIO_DEVICE_REMOVED;
			device.ProcessEvent(event);
			if (device.IsReady() || device.GetState(voice.Voice) != VoiceState::Invalid || !(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO)) return false;
			event.type = SDL_EVENT_AUDIO_DEVICE_ADDED;
			device.ProcessEvent(event);
			device.Update();
			if (!device.IsReady()) return false;
		}
		return true;
	}

	bool AudioStreamingOfflineSelfCheck()
	{
		SDL_AudioSpec spec{SDL_AUDIO_F32, 2, 48000};
		auto stream = std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)>(SDL_CreateAudioStream(&spec, &spec), SDL_DestroyAudioStream);
		if (!stream) return false;
		AudioDevice device;
		AudioDevice::Impl::Slot slot;
		slot.Stream = stream.get(); slot.SampleRate = 48000; slot.Ring.resize(12000);
		slot.Left = slot.TargetLeft = 1; slot.Right = slot.TargetRight = .25f;
		slot.ReadHead = 11998; slot.Buffered = 4;
		const float samples[] = {.25f, -.5f, .75f, -1.f};
		for (size_t i = 0; i < 4; ++i) slot.Ring[(slot.ReadHead + i) % slot.Ring.size()] = samples[i];
		AudioDevice::Impl::Fill(&slot, stream.get(), 8 * 8, 0);
		std::array<float, 16> first{};
		if (SDL_GetAudioStreamData(stream.get(), first.data(), int(sizeof(first))) != sizeof(first)) return false;
		for (size_t i = 0; i < 8; ++i)
		{
			const float value = i < 4 ? samples[i] : 0;
			if (first[i * 2] != value || first[i * 2 + 1] != value * .25f) return false;
		}
		if (slot.Buffered || slot.Ended || slot.Cursor != 8) return false;
		slot.Params.LowPassHz = 500; slot.Params.RightGain = 1;
		device.m_Impl->Targets(slot);
		std::array<float, 1920> output{};
		for (int pass = 0; pass < 2; ++pass)
		{
			slot.ReadHead = 0; slot.Buffered = 960;
			for (size_t i = 0; i < 960; ++i) slot.Ring[i] = i % 2 ? -1.f : 1.f;
			AudioDevice::Impl::Fill(&slot, stream.get(), 960 * 8, 0);
			if (SDL_GetAudioStreamData(stream.get(), output.data(), int(sizeof(output))) != sizeof(output)) return false;
			for (float value : output) if (!std::isfinite(value) || std::abs(value) > 1) return false;
			if (pass == 0 && std::abs(output.back()) > .1f) return false;
			if (pass == 1 && std::abs(output.back()) < .99f) return false;
			slot.Params.LowPassHz = 0;
			device.m_Impl->Targets(slot);
		}
		device.m_Impl->Master = .5f; device.m_Impl->BusGains[static_cast<size_t>(AudioBus::Voice)] = .5f;
		slot.Params.Bus = AudioBus::Voice; slot.Params.Volume = .8f; slot.Params.LeftGain = .5f; slot.Params.RightGain = .25f;
		device.m_Impl->Targets(slot);
		slot.ReadHead = 0; slot.Buffered = 960;
		std::fill_n(slot.Ring.begin(), 960, 1.f);
		AudioDevice::Impl::Fill(&slot, stream.get(), 960 * 8, 0);
		if (SDL_GetAudioStreamData(stream.get(), output.data(), int(sizeof(output))) != sizeof(output)) return false;
		return std::abs(output[1918] - .1f) < .00001f && std::abs(output[1919] - .05f) < .00001f;
	}

	AudioDevice::AudioDevice() : m_Impl(std::make_unique<Impl>()) {}
	AudioDevice::~AudioDevice() { Shutdown(); }
	AudioResult AudioDevice::Initialize()
	{
		m_Impl->CheckThread();
		if (IsReady()) return {};
		if (!m_Impl->Initialized)
		{
			if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) { m_Impl->Error = SDL_GetError(); return {AudioError::NotReady, m_Impl->Error}; }
			m_Impl->Initialized = true;
		}
		SDL_AudioSpec preferred{SDL_AUDIO_F32, 2, 48000};
		m_Impl->Device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &preferred);
		if (!m_Impl->Device || !SDL_SetAudioPostmixCallback(m_Impl->Device, Impl::Postmix, m_Impl.get()) || !SDL_ResumeAudioDevice(m_Impl->Device))
		{
			m_Impl->Error = SDL_GetError();
			if (m_Impl->Device) SDL_CloseAudioDevice(m_Impl->Device);
			m_Impl->Device = 0;
			return {AudioError::NotReady, m_Impl->Error};
		}
		SDL_AudioSpec actual{};
		if (SDL_GetAudioDeviceFormat(m_Impl->Device, &actual, nullptr)) { m_Impl->OutputRate = actual.freq; m_Impl->OutputChannels = actual.channels; }
		m_Impl->Error.Clear();
		return {};
	}
	void AudioDevice::Shutdown()
	{
		m_Impl->CheckThread();
		m_Impl->CloseOutput();
		if (m_Impl->Initialized) { SDL_QuitSubSystem(SDL_INIT_AUDIO); m_Impl->Initialized = false; }
		m_Impl->RetryPending = false;
	}
	bool AudioDevice::IsReady() const { return m_Impl->Device != 0; }
	AudioClipCache& AudioDevice::GetClipCache() { m_Impl->CheckThread(); return m_Impl->Cache; }
	std::weak_ptr<void> AudioDevice::GetLifetimeToken() const { return m_Impl->Lifetime; }

	AudioPlayResult AudioDevice::Play(uint64 context, const std::shared_ptr<const AudioClip>& clip, const AudioPlaybackParams& params, VoiceHandle replace, bool startPaused)
	{
		m_Impl->CheckThread();
		if (!clip || !clip->Frames || clip->Channels < 1 || clip->Channels > 2 || clip->SampleRate < 8000 ||
			clip->SampleRate > 96000 || clip->Frames > 96000ull * 60 || clip->Samples.size() != clip->Frames * clip->Channels)
			return {{}, {AudioError::MissingClip, "A validated nonempty audio clip is required."}};
		return StartVoice(context, clip, clip->SampleRate, params, replace, startPaused);
	}
	AudioPlayResult AudioDevice::CreateStream(uint64 context, int sampleRate, const AudioPlaybackParams& params)
	{
		return StartVoice(context, {}, sampleRate, params, {}, false);
	}
	AudioPlayResult AudioDevice::StartVoice(uint64 context, const std::shared_ptr<const AudioClip>& clip, int sampleRate,
		const AudioPlaybackParams& params, VoiceHandle replace, bool startPaused)
	{
		m_Impl->CheckThread();
		auto fail = [&](AudioError code, const char* message) -> AudioPlayResult { ++m_Impl->Rejected; return {{}, {code, message}}; };
		if (!IsReady()) return fail(AudioError::NotReady, "Audio output is unavailable; retry audio initialization.");
		if (!context || !Valid(params)) return fail(AudioError::InvalidSettings, "Invalid audio context, gain, pitch, bus or priority.");
		if (sampleRate < 8000 || sampleRate > 96000) return fail(AudioError::InvalidSettings, "Streaming audio rate must be 8,000-96,000 Hz.");
		uint32 selected = PreviewSlot;
		bool stealing = false;
		if (!params.Preview)
		{
			if (replace.Context == context && replace.Slot < GameVoices && m_Impl->Find(replace)) selected = replace.Slot;
			else
			{
				for (uint32 i = 0; i < GameVoices; ++i) if (!m_Impl->Voices[i]) { selected = i; break; }
				if (selected == PreviewSlot)
				{
					selected = 0;
					for (uint32 i = 1; i < GameVoices; ++i)
					{
						const auto& a = m_Impl->Voices[i]; const auto& b = m_Impl->Voices[selected];
						if (a->Params.Priority < b->Params.Priority || (a->Params.Priority == b->Params.Priority && a->Serial < b->Serial)) selected = i;
					}
					if (params.Priority <= m_Impl->Voices[selected]->Params.Priority) return fail(AudioError::VoiceLimit, "All 64 game voice slots have equal or higher priority.");
					stealing = true;
				}
			}
		}
		auto incoming = std::make_unique<Impl::Slot>();
		incoming->Clip = clip;
		incoming->SampleRate = sampleRate;
		if (!clip) incoming->Ring.resize(static_cast<size_t>(sampleRate / 4));
		incoming->Params = params;
		incoming->Paused = startPaused;
		incoming->Context = context;
		incoming->Generation = ++m_Impl->Generation;
		incoming->Serial = ++m_Impl->Serial;
		m_Impl->Targets(*incoming);
		SDL_AudioSpec input{SDL_AUDIO_F32, 2, sampleRate};
		SDL_AudioSpec output{SDL_AUDIO_F32, 2, 48000};
		incoming->Stream = SDL_CreateAudioStream(&input, &output);
		if (!incoming->Stream) return fail(AudioError::BackendFailure, SDL_GetError());
		if (!SDL_SetAudioStreamFrequencyRatio(incoming->Stream, params.Pitch) ||
			!SDL_SetAudioStreamGetCallback(incoming->Stream, Impl::Fill, incoming.get()) ||
			!SDL_BindAudioStream(m_Impl->Device, incoming->Stream))
		{
			const String error = SDL_GetError();
			SDL_SetAudioStreamGetCallback(incoming->Stream, nullptr, nullptr);
			SDL_DestroyAudioStream(incoming->Stream);
			return fail(AudioError::BackendFailure, error.CStr());
		}
		if (startPaused) SDL_UnbindAudioStream(incoming->Stream);
		m_Impl->Retire(selected);
		const VoiceHandle handle{context, incoming->Generation, selected};
		m_Impl->Voices[selected] = std::move(incoming);
		if (stealing) ++m_Impl->Stolen;
		return {handle, {}};
	}
	bool AudioDevice::QueueStream(VoiceHandle handle, const float* samples, uint32 count)
	{
		m_Impl->CheckThread();
		auto* slot = m_Impl->Find(handle);
		if (!slot || slot->Clip || count > slot->Ring.size() || (count && !samples)) return false;
		for (uint32 i = 0; i < count; ++i) if (!std::isfinite(samples[i])) return false;
		if (!SDL_LockAudioStream(slot->Stream)) return false;
		const bool fits = count <= slot->Ring.size() - slot->Buffered;
		if (fits)
		{
			for (uint32 i = 0; i < count; ++i)
				slot->Ring[(slot->ReadHead + slot->Buffered + i) % slot->Ring.size()] = std::clamp(samples[i], -1.f, 1.f);
			slot->Buffered += count;
		}
		SDL_UnlockAudioStream(slot->Stream);
		return fits;
	}
	bool AudioDevice::SetParameters(VoiceHandle handle, const AudioPlaybackParams& params)
	{
		m_Impl->CheckThread();
		auto* slot = m_Impl->Find(handle);
		if (!slot || !Valid(params) || params.Preview != slot->Params.Preview) return false;
		if (!SDL_LockAudioStream(slot->Stream)) return false;
		const bool success = SDL_SetAudioStreamFrequencyRatio(slot->Stream, params.Pitch);
		if (success) { slot->Params = params; m_Impl->Targets(*slot); }
		SDL_UnlockAudioStream(slot->Stream);
		return success;
	}
	bool AudioDevice::Stop(VoiceHandle voice)
	{
		// shortcut: immediate stops can click; add bounded fades when a game needs smooth retirement.
		m_Impl->CheckThread();
		if (!m_Impl->Find(voice)) return false;
		m_Impl->Retire(voice.Slot);
		return true;
	}
	bool AudioDevice::Pause(VoiceHandle voice)
	{
		m_Impl->CheckThread();
		auto* slot = m_Impl->Find(voice);
		if (!slot) return false;
		if (!slot->Paused)
		{
			SDL_UnbindAudioStream(slot->Stream);
			if (!SDL_LockAudioStream(slot->Stream)) return false;
			slot->Paused = true;
			SDL_UnlockAudioStream(slot->Stream);
		}
		return true;
	}
	bool AudioDevice::Resume(VoiceHandle voice)
	{
		m_Impl->CheckThread();
		auto* slot = m_Impl->Find(voice);
		if (!slot) return false;
		if (slot->Paused)
		{
			if (!SDL_LockAudioStream(slot->Stream)) return false;
			slot->Paused = false;
			SDL_UnlockAudioStream(slot->Stream);
			if (!SDL_BindAudioStream(m_Impl->Device, slot->Stream))
			{
				SDL_LockAudioStream(slot->Stream);
				slot->Paused = true;
				SDL_UnlockAudioStream(slot->Stream);
				return false;
			}
		}
		return true;
	}
	VoiceState AudioDevice::GetState(VoiceHandle voice) const
	{
		m_Impl->CheckThread();
		auto* slot = m_Impl->Find(voice);
		if (!slot) return VoiceState::Invalid;
		if (slot->Paused) return VoiceState::Paused;
		if (!SDL_LockAudioStream(slot->Stream)) return VoiceState::Invalid;
		const bool ended = slot->Ended;
		SDL_UnlockAudioStream(slot->Stream);
		return ended ? VoiceState::Draining : VoiceState::Playing;
	}
	void AudioDevice::StopContext(uint64 context)
	{
		m_Impl->CheckThread();
		for (uint32 i = 0; i < m_Impl->Voices.size(); ++i) if (m_Impl->Voices[i] && m_Impl->Voices[i]->Context == context) m_Impl->Retire(i);
	}
	void AudioDevice::Update()
	{
		m_Impl->CheckThread();
		if (m_Impl->RetryPending) { m_Impl->RetryPending = false; Initialize(); }
		if (!IsReady()) return;
		for (uint32 i = 0; i < m_Impl->Voices.size(); ++i)
		{
			auto* slot = m_Impl->Voices[i].get();
			if (!slot || !SDL_LockAudioStream(slot->Stream)) continue;
			const bool failed = slot->Failed;
			const bool done = !slot->Paused && slot->Ended && SDL_GetAudioStreamQueued(slot->Stream) == 0 && SDL_GetAudioStreamAvailable(slot->Stream) == 0;
			SDL_UnlockAudioStream(slot->Stream);
			if (failed) { m_Impl->Error = "An audio stream callback failed; the affected voice was stopped."; FP_LOG_WARN("%s", m_Impl->Error.CStr()); }
			if (done || failed) m_Impl->Retire(i);
		}
	}
	bool AudioDevice::SetBusGain(AudioBus bus, float gain)
	{
		m_Impl->CheckThread();
		if (uint8(bus) > uint8(AudioBus::Voice) || !Gain(gain)) return false;
		m_Impl->BusGains[static_cast<size_t>(bus)] = gain;
		m_Impl->RefreshGains();
		return true;
	}
	void AudioDevice::SetBusMuted(AudioBus bus, bool muted)
	{
		m_Impl->CheckThread();
		if (uint8(bus) > uint8(AudioBus::Voice)) return;
		m_Impl->BusMuted[static_cast<size_t>(bus)] = muted;
		m_Impl->RefreshGains();
	}
	bool AudioDevice::SetMasterGain(float gain)
	{
		m_Impl->CheckThread();
		if (!Gain(gain)) return false;
		m_Impl->Master = gain;
		m_Impl->RefreshGains();
		return true;
	}
	void AudioDevice::SetMasterMuted(bool muted) { m_Impl->CheckThread(); m_Impl->MasterMuted = muted; m_Impl->RefreshGains(); }
	String AudioDevice::GetStatusText() const
	{
		m_Impl->CheckThread();
		uint32 active = 0, paused = 0, draining = 0;
		uint64 overruns = m_Impl->Overruns;
		for (uint32 i = 0; i < m_Impl->Voices.size(); ++i) if (const auto& slot = m_Impl->Voices[i])
		{
			++active;
			const auto state = GetState({slot->Context, slot->Generation, i});
			paused += state == VoiceState::Paused;
			draining += state == VoiceState::Draining;
			if (SDL_LockAudioStream(slot->Stream)) { overruns += slot->Overruns; SDL_UnlockAudioStream(slot->Stream); }
		}
		const char* name = IsReady() ? SDL_GetAudioDeviceName(m_Impl->Device) : nullptr;
		return String::Format("%s: %s, %d Hz / %d channels\nVoices %u (paused %u, draining %u); resident %llu, pinned %llu bytes\nRejected %llu, stolen %llu, demand overruns %llu, clipped %llu, nonfinite %llu%s%s",
			IsReady() ? "Ready" : "Unavailable", name ? name : "default output", m_Impl->OutputRate.load(), m_Impl->OutputChannels.load(), active, paused, draining,
			static_cast<unsigned long long>(m_Impl->Cache.ResidentBytes()), static_cast<unsigned long long>(m_Impl->Cache.PinnedBytes()),
			static_cast<unsigned long long>(m_Impl->Rejected), static_cast<unsigned long long>(m_Impl->Stolen), static_cast<unsigned long long>(overruns),
			static_cast<unsigned long long>(m_Impl->Clipped.load()), static_cast<unsigned long long>(m_Impl->Nonfinite.load()), m_Impl->Error.IsEmpty() ? "" : "\n", m_Impl->Error.CStr());
	}
	void AudioDevice::ProcessEvent(const SDL_Event& event)
	{
		m_Impl->CheckThread();
		if (event.type != SDL_EVENT_AUDIO_DEVICE_ADDED && event.type != SDL_EVENT_AUDIO_DEVICE_REMOVED && event.type != SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED) return;
		if (event.adevice.recording) return;
		if (IsReady())
		{
			SDL_AudioSpec current{};
			if (!SDL_GetAudioDeviceFormat(m_Impl->Device, &current, nullptr)) { m_Impl->Error = SDL_GetError(); m_Impl->CloseOutput(); }
			else { m_Impl->OutputRate = current.freq; m_Impl->OutputChannels = current.channels; }
		}
		if (!IsReady() && event.type == SDL_EVENT_AUDIO_DEVICE_ADDED) m_Impl->RetryPending = true;
	}
}
