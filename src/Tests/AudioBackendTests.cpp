#include "Tests/TestFramework.h"
#include "Tests/TestProject.h"
#include "Engine/Assets/AssetDatabase.h"
#include "Engine/Audio/AudioClip.h"
#include "Engine/Audio/AudioDevice.h"

#include <SDL3/SDL.h>
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>

using namespace FP;

namespace FP { bool AudioBackendOfflineSelfCheck(); }

namespace
{
	void U16(std::vector<uint8>& bytes, uint16 value) { bytes.push_back(uint8(value)); bytes.push_back(uint8(value >> 8)); }
	void U32(std::vector<uint8>& bytes, uint32 value) { U16(bytes, uint16(value)); U16(bytes, uint16(value >> 16)); }
	void Tag(std::vector<uint8>& bytes, const char* tag) { bytes.insert(bytes.end(), tag, tag + 4); }
	std::vector<uint8> Wav(uint16 channels = 1, uint32 rate = 8000, bool floating = false, uint32 frames = 16, bool metadata = false)
	{
		std::vector<uint8> bytes;
		Tag(bytes, "RIFF"); U32(bytes, 0); Tag(bytes, "WAVE");
		if (metadata) { Tag(bytes, "JUNK"); U32(bytes, 1); bytes.push_back(123); bytes.push_back(0); }
		Tag(bytes, "fmt "); U32(bytes, 16); U16(bytes, floating ? 3 : 1); U16(bytes, channels); U32(bytes, rate);
		const uint16 alignment = uint16(channels * (floating ? 4 : 2));
		U32(bytes, rate * alignment); U16(bytes, alignment); U16(bytes, floating ? 32 : 16);
		Tag(bytes, "data"); U32(bytes, frames * alignment);
		for (uint32 i = 0; i < frames * channels; ++i)
		{
			if (floating) U32(bytes, std::bit_cast<uint32>(i % 2 ? -.5f : .5f));
			else U16(bytes, i % 2 ? uint16(-16384) : uint16(16384));
		}
		const uint32 riff = uint32(bytes.size() - 8);
		for (int i = 0; i < 4; ++i) bytes[size_t(i) + 4] = uint8(riff >> (i * 8));
		return bytes;
	}
	String Utf8(const std::filesystem::path& path)
	{
		const auto text = path.generic_u8string();
		return {reinterpret_cast<const char*>(text.data()), int32(text.size())};
	}
	struct Fixtures
	{
		std::filesystem::path Root = std::filesystem::temp_directory_path() / ("FirstPrinciplesAudio-" + std::to_string(SDL_GetTicksNS()));
		Fixtures() { std::filesystem::create_directories(Root); }
		~Fixtures() { std::error_code ec; std::filesystem::remove_all(Root, ec); }
		String Write(const std::filesystem::path& name, const std::vector<uint8>& bytes)
		{
			const auto path = Root / name;
			std::filesystem::create_directories(path.parent_path());
			std::ofstream file(path, std::ios::binary);
			file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
			return Utf8(path);
		}
	};
}

FP_TEST(Audio_OfflineCallbackLoopDrainRampAndPitch)
{
	FP_EXPECT(AudioBackendOfflineSelfCheck());
}

FP_TEST(Audio_WavFormatsAndUnicode)
{
	Fixtures fixtures;
	AudioClipCache cache(Utf8(fixtures.Root));
	for (uint16 channels : {uint16(1), uint16(2)}) for (uint32 rate : {8000u, 96000u}) for (bool floating : {false, true})
	{
		const String path = fixtures.Write(u8"Audio/音声 café.wav", Wav(channels, rate, floating, 1, true));
		const auto decoded = AudioClipCache::ValidateFile(path);
		FP_EXPECT(decoded);
		if (!decoded) continue;
		FP_EXPECT(decoded.Clip->Frames == 1 && decoded.Clip->Channels == channels && decoded.Clip->SampleRate == int(rate));
		FP_EXPECT(decoded.Clip->Samples.size() == channels);
		FP_EXPECT(std::abs(decoded.Clip->Samples[0] - .5f) < .0001f);
	}
	const auto loaded = cache.Load("Audio/音声 café.wav");
	FP_EXPECT(loaded);
	if (loaded) FP_EXPECT(loaded.Clip->Path == "Audio/音声 café.wav");
	FP_EXPECT(!cache.Load("../outside.wav"));
	FP_EXPECT(!cache.Load("C:relative.wav"));
	FP_EXPECT(!cache.Load("/absolute.wav"));
	FP_EXPECT(!cache.Load(String("Audio/a\0.wav", 12)));
	FP_EXPECT(!cache.Load("audio/音声 café.wav"));
}

FP_TEST(Audio_WavRejectsMalformedAndNonfinite)
{
	Fixtures fixtures;
	auto check = [&](std::vector<uint8> bytes)
	{
		FP_EXPECT(!AudioClipCache::ValidateFile(fixtures.Write("invalid.wav", bytes)));
	};
	check(Wav(1, 7999));
	check(Wav(3));
	check(Wav(1, 8000, false, 0));
	const auto longClip = AudioClipCache::ValidateFile(fixtures.Write("long.wav", Wav(1, 8000, false, 8000 * 60 + 1)));
	FP_EXPECT(longClip && longClip.Clip->IsStreaming() && longClip.Clip->Samples.empty());
	auto bytes = Wav(); bytes.pop_back(); check(bytes);
	bytes = Wav(); bytes[32] = 1; check(bytes);
	bytes = Wav(); bytes[20] = 17; check(bytes);
	bytes = Wav(); bytes[40] = 0xff; bytes[41] = 0xff; bytes[42] = 0xff; bytes[43] = 0x7f; check(bytes);
	for (float sample : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
	{
		bytes = Wav(1, 8000, true);
		const uint32 bits = std::bit_cast<uint32>(sample);
		for (int i = 0; i < 4; ++i) bytes[size_t(i) + 44] = uint8(bits >> (i * 8));
		check(bytes);
	}
	bytes = Wav(1, 8000, true);
	const uint32 loud = std::bit_cast<uint32>(2.f);
	for (int i = 0; i < 4; ++i) bytes[size_t(i) + 44] = uint8(loud >> (i * 8));
	const auto clipped = AudioClipCache::ValidateFile(fixtures.Write("clipped.wav", bytes));
	FP_EXPECT(clipped);
	if (clipped) FP_EXPECT(clipped.Clip->ClippedSamples == 1 && clipped.Clip->Samples[0] == 1.f);
}

FP_TEST(Audio_CachePinsBudgetReloadAndFailureBoundary)
{
	Fixtures fixtures;
	fixtures.Write("a.wav", Wav()); fixtures.Write("b.wav", Wav());
	AudioClipCache cache(Utf8(fixtures.Root), 128);
	auto first = cache.Load("a.wav");
	FP_EXPECT(first);
	const auto reused = cache.Load("a.wav");
	FP_EXPECT(reused.Clip == first.Clip);
	FP_EXPECT(cache.ResidentBytes() == 64 && cache.PinnedBytes() == 64);
	auto second = cache.Load("b.wav");
	FP_EXPECT(second);
	FP_EXPECT(cache.ResidentBytes() == 128);
	FP_EXPECT(!cache.Load("a.wav", true));
	FP_EXPECT(cache.Load("a.wav").Clip == first.Clip);
	second.Clip.reset();
	const auto replacement = cache.Load("a.wav", true);
	FP_EXPECT(replacement);
	if (replacement && first) FP_EXPECT(replacement.Clip->Revision > first.Clip->Revision && replacement.Clip != first.Clip);
	FP_EXPECT(cache.ResidentBytes() == 128 && cache.PinnedBytes() == 128);
	fixtures.Write("a.wav", {1, 2, 3});
	FP_EXPECT(!cache.Load("a.wav", true));
	FP_EXPECT(cache.Load("a.wav").Clip == replacement.Clip);
	std::filesystem::remove(fixtures.Root / "a.wav");
	FP_EXPECT(!cache.Load("a.wav", true));
	FP_EXPECT(cache.Load("a.wav").Clip == replacement.Clip);
	FP_EXPECT(!cache.Load("missing.wav"));
	fixtures.Write("missing.wav", Wav());
	FP_EXPECT(!cache.Load("missing.wav"));
	cache.ClearFailures();
	AudioClipCache roomy(Utf8(fixtures.Root));
	FP_EXPECT(roomy.Load("missing.wav"));
	fixtures.Write("retry.wav", {1, 2, 3});
	FP_EXPECT(!roomy.Load("retry.wav"));
	fixtures.Write("retry.wav", Wav());
	FP_EXPECT(!roomy.Load("retry.wav"));
	roomy.ClearFailures();
	FP_EXPECT(roomy.Load("retry.wav"));
	AudioClipCache transient(Utf8(fixtures.Root), 128 * 1024 * 1024, 64);
	FP_EXPECT(!transient.Load("missing.wav"));
}

FP_TEST(Audio_CacheRetriesAfterPinnedMemoryIsReleased)
{
	Fixtures fixtures;
	fixtures.Write("a.wav", Wav()); fixtures.Write("b.wav", Wav());
	AudioClipCache cache(Utf8(fixtures.Root), 64);
	auto pinned = cache.Load("a.wav");
	FP_EXPECT(pinned);
	FP_EXPECT(cache.Load("b.wav").Result.Code == AudioError::BudgetExceeded);
	pinned.Clip.reset();
	FP_EXPECT(cache.Load("b.wav"));
	FP_EXPECT(cache.ResidentBytes() == 64);
}

FP_TEST(Audio_CacheAliasesSurviveOfflineFilesAndRevalidateAfterEviction)
{
	Fixtures fixtures;
	fixtures.Write("Audio/a.wav", Wav()); fixtures.Write("b.wav", Wav());
	AudioClipCache cache(Utf8(fixtures.Root), 64);
	auto first = cache.Load("Audio/a.wav");
	FP_EXPECT(first);
	FP_EXPECT(cache.Load("Audio/./a.wav").Clip == first.Clip);
	std::filesystem::rename(fixtures.Root / "Audio", fixtures.Root / "Offline");
	FP_EXPECT(cache.Load("Audio/a.wav").Clip == first.Clip);
	FP_EXPECT(cache.Load("Audio/./a.wav").Clip == first.Clip);
	FP_EXPECT(!cache.Load("Audio/./a.wav", true));
	FP_EXPECT(cache.Load("Audio/./a.wav").Clip == first.Clip);
	FP_EXPECT(!cache.Load("audio/a.wav"));
	FP_EXPECT(!cache.Load("../Offline/a.wav"));
	first.Clip.reset();
	FP_EXPECT(cache.Load("b.wav"));
	FP_EXPECT(!cache.Load("Audio/./a.wav"));
}

FP_TEST(Audio_CachedAssetReferencesReimportAndSurviveMoves)
{
	Test::ScopedProject project;
	auto write = [](const char* name, uint32 frames) {
		const auto bytes = Wav(1, 8000, false, frames);
		std::ofstream file(Paths::Content(name).CStr(), std::ios::binary);
		file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
	};
	write("tone.wav", 16);
	FP_EXPECT(Paths::GetAssets().Open(Paths::GetContentDir(), true));
	const String reference = Paths::AssetReference("tone.wav");
	FP_EXPECT(reference.StartsWith("asset://"));
	AudioClipCache cache;
	const auto first = cache.Load(reference);
	FP_EXPECT(first && first.Clip->Frames == 16);
	FP_EXPECT(cache.Load("tone.wav").Clip == first.Clip);
	write("tone.wav", 32);
	FP_EXPECT(Paths::GetAssets().Refresh());
	const auto changed = cache.Load(reference);
	FP_EXPECT(changed && changed.Clip != first.Clip && changed.Clip->Frames == 32);
	String error;
	FP_EXPECT(Paths::GetAssets().Move("tone.wav", "renamed.wav", error));
	const auto moved = cache.Load(reference);
	FP_EXPECT(moved && moved.Clip->Path == "renamed.wav" && moved.Clip->Frames == 32);
	FP_EXPECT(cache.Load("tone.wav").Clip == moved.Clip);
	std::filesystem::remove(Paths::Content("renamed.wav").CStr());
	FP_EXPECT(cache.Load(reference).Clip == moved.Clip);
	FP_EXPECT(!cache.Load(reference, true));
}

FP_TEST(Audio_DummyDeviceAdmissionPauseAndShutdown)
{
	// Dummy output establishes lifecycle behavior without sending test tones to speakers.
	const char* previousDriver = SDL_GetHint(SDL_HINT_AUDIO_DRIVER);
	const String savedDriver = previousDriver ? previousDriver : "";
	SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
	FP_EXPECT(AudioBackendOfflineSelfCheck());
	{
		Fixtures fixtures;
		const auto decoded = AudioClipCache::ValidateFile(fixtures.Write("loop.wav", Wav(1, 8000, false, 8000)));
		FP_EXPECT(decoded);
		AudioDevice device;
		FP_EXPECT(device.Initialize());
		if (decoded && device.IsReady())
		{
			AudioPlaybackParams params; params.Loop = true;
			std::vector<VoiceHandle> voices;
			for (int i = 0; i < 64; ++i)
			{
				const auto voice = device.Play(1, decoded.Clip, params);
				FP_EXPECT(voice);
				voices.push_back(voice.Voice);
			}
			FP_EXPECT(device.Play(1, decoded.Clip, params).Result.Code == AudioError::VoiceLimit);
			FP_EXPECT(device.Pause(voices[0]));
			SDL_Delay(30);
			FP_EXPECT(device.GetState(voices[0]) == VoiceState::Paused);
			FP_EXPECT(device.Resume(voices[0]));
			FP_EXPECT(device.GetState(voices[0]) == VoiceState::Playing);
			AudioPlaybackParams invalid = params; invalid.Pitch = 0;
			FP_EXPECT(!device.Play(1, decoded.Clip, invalid, voices[0]));
			FP_EXPECT(device.GetState(voices[0]) == VoiceState::Playing);
			params.Priority = 129;
			const auto stolen = device.Play(2, decoded.Clip, params);
			FP_EXPECT(stolen && device.GetState(voices[0]) == VoiceState::Invalid);
			FP_EXPECT(!device.Stop(voices[0]));
			params.Preview = true;
			const auto preview = device.Play(3, decoded.Clip, params);
			FP_EXPECT(preview);
			device.StopContext(1);
			FP_EXPECT(device.GetState(stolen.Voice) == VoiceState::Playing);
			FP_EXPECT(device.GetState(preview.Voice) == VoiceState::Playing);
			FP_EXPECT(!device.SetMasterGain(std::numeric_limits<float>::quiet_NaN()));
			FP_EXPECT(device.SetBusGain(AudioBus::SFX, .25f));
			device.SetMasterMuted(true);
			device.Shutdown();
			FP_EXPECT(device.GetState(stolen.Voice) == VoiceState::Invalid);
			FP_EXPECT(!device.IsReady());
			FP_EXPECT(device.Initialize());
			for (int i = 0; i < 100; ++i)
			{
				const auto voice = device.Play(4, decoded.Clip, params);
				FP_EXPECT(voice);
				device.StopContext(4);
				FP_EXPECT(device.GetState(voice.Voice) == VoiceState::Invalid);
			}
		}
	}
	if (savedDriver.IsEmpty()) SDL_ResetHint(SDL_HINT_AUDIO_DRIVER); else SDL_SetHint(SDL_HINT_AUDIO_DRIVER, savedDriver.CStr());
}
