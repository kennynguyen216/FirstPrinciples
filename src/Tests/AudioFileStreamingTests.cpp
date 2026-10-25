#include "Tests/TestFramework.h"
#include "Editor/AudioImport.h"
#include "Engine/Engine.h"
#include "Engine/Audio/AudioDevice.h"
#include "Engine/Audio/AudioFileStream.h"
#include "Engine/Paths.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>

using namespace FP;
namespace FP { bool AudioFileMixerOfflineSelfCheck(const std::shared_ptr<const AudioClip>& clip); }

namespace
{
	namespace fs = std::filesystem;
	String Utf8(const fs::path& path)
	{
		const auto text = path.generic_u8string();
		return {reinterpret_cast<const char*>(text.data()), int32(text.size())};
	}
	fs::path Native(const String& text) { return fs::path(reinterpret_cast<const char8_t*>(text.CStr())); }
	struct Fixture
	{
		fs::path Directory = Native(Paths::Content("Audio")) / Guid::New().ToString().CStr();
		Fixture() { fs::create_directories(Directory); }
		~Fixture() { std::error_code ec; fs::remove_all(Directory, ec); }
		String Relative(const char* name) const { return Utf8(fs::path("Audio") / Directory.filename() / name); }
		fs::path Wav(const char* name, uint16 channels = 2, uint32 frames = 8000 * 61, uint32 rate = 8000)
		{
			std::vector<uint8> header;
			auto u16 = [&](uint16 v) { header.push_back(uint8(v)); header.push_back(uint8(v >> 8)); };
			auto u32 = [&](uint32 v) { u16(uint16(v)); u16(uint16(v >> 16)); };
			auto tag = [&](const char* v) { header.insert(header.end(), v, v + 4); };
			tag("RIFF"); u32(36 + frames * channels * 2); tag("WAVE"); tag("fmt "); u32(16);
			u16(1); u16(channels); u32(rate); u32(rate * channels * 2); u16(channels * 2); u16(16);
			tag("data"); u32(frames * channels * 2);
			const auto path = Directory / name;
			std::ofstream file(path, std::ios::binary);
			file.write(reinterpret_cast<const char*>(header.data()), std::streamsize(header.size()));
			const char sample[] = {0, 0x20, 0, char(0xc0)}; // L=.25, R=-.5, or mono=.25.
			for (uint32 i = 0; i < frames; ++i) file.write(sample, channels * 2);
			return path;
		}
		fs::path Mp3()
		{
			const auto path = Directory / "Long.mp3";
			fs::copy_file(Native(Paths::GetEngineDir()).parent_path() / "src/Tests/Fixtures/Audio/TooLong.mp3", path);
			return path;
		}
	};

	uint64 Drain(AudioFileStream& stream, int channels, bool verifyWav = false)
	{
		std::array<float, 1024> samples{};
		uint64 total = 0;
		const uint64 deadline = SDL_GetTicks() + 10000;
		while ((!stream.Finished() || stream.BufferedFrames()) && !stream.Failed() && SDL_GetTicks() < deadline)
		{
			const uint32 count = stream.Read(samples.data(), uint32(samples.size() / channels));
			if (verifyWav)
				for (uint32 i = 0; i < count; ++i)
				{
					FP_EXPECT(samples[i * channels] == .25f);
					if (channels == 2) FP_EXPECT(samples[i * channels + 1] == -.5f);
				}
			total += count;
			if (!count) SDL_Delay(1);
		}
		FP_EXPECT(stream.Finished() && !stream.Failed());
		return total;
	}
}

FP_TEST(AudioFileStreaming_LongWavAndMp3StayBoundedPastSixtySeconds)
{
	Fixture fixture;
	for (const auto& path : {fixture.Wav("Long.wav"), fixture.Mp3()})
	{
		const auto loaded = AudioClipCache::ValidateFile(Utf8(path));
		FP_EXPECT(loaded);
		if (!loaded) continue;
		FP_EXPECT(loaded.Clip->IsStreaming() && loaded.Clip->Duration > 60 && loaded.Clip->Samples.empty());
		AudioClipCache cache(Utf8(fixture.Directory));
		FP_EXPECT(cache.Load(Utf8(path.filename())));
		FP_EXPECT(cache.ResidentBytes() == 0);
		AudioResult result;
		auto stream = AudioFileStream::Open(*loaded.Clip, false, result);
		FP_EXPECT(stream && result);
		if (!stream) continue;
		FP_EXPECT(stream->BufferBytes() == uint64(loaded.Clip->SampleRate * loaded.Clip->Channels) * sizeof(float));
		FP_EXPECT(stream->BufferBytes() <= 96000 * 2 * sizeof(float));
		FP_EXPECT(Drain(*stream, loaded.Clip->Channels, path.extension() == ".wav") == loaded.Clip->Frames);
		if (path.extension() == ".wav") FP_EXPECT(AudioFileMixerOfflineSelfCheck(loaded.Clip));
	}
	fixture.Wav("Large.wav", 2, 45 * 96000, 96000);
	AudioClipCache cache(Utf8(fixture.Directory), 1);
	const auto loaded = cache.Load("Large.wav");
	FP_EXPECT(loaded && loaded.Clip->Duration < 60 && loaded.Clip->IsStreaming() && loaded.Clip->Samples.empty());
	FP_EXPECT(cache.ResidentBytes() == 0);
}

FP_TEST(AudioFileStreaming_LoopWrapsWithoutDroppingSamplesAndCanStopLooping)
{
	Fixture fixture;
	const auto path = fixture.Wav("Tiny.wav", 2, 3);
	AudioFileDecoder decoder; FP_EXPECT(decoder.Open(Utf8(path)));
	AudioClip clip = decoder.Info(); clip.StreamPath = Utf8(path);
	AudioResult result;
	auto stream = AudioFileStream::Open(clip, true, result);
	FP_EXPECT(stream && result);
	if (!stream) return;
	std::array<float, 256> samples{};
	uint64 frames = 0;
	const uint64 deadline = SDL_GetTicks() + 3000;
	while (frames < 128 && SDL_GetTicks() < deadline)
	{
		const uint32 count = stream->Read(samples.data(), 128);
		for (uint32 i = 0; i < count; ++i) FP_EXPECT(samples[i * 2] == .25f && samples[i * 2 + 1] == -.5f);
		frames += count;
		if (!count) SDL_Delay(1);
	}
	FP_EXPECT(frames >= 128 && !stream->Finished() && !stream->Failed());
	stream->SetLoop(false);
	FP_EXPECT(Drain(*stream, 2, true) <= 8003);
}

FP_TEST(AudioFileStreaming_ImportLongFilesAndRejectChangedOrMissingBackingFile)
{
	Fixture fixture;
	const auto source = fixture.Mp3();
	const auto destination = fixture.Directory / "Imported.mp3";
	String relative;
	FP_EXPECT(Editor::ImportAudioFile(Utf8(source), Utf8(destination), relative));
	const auto loaded = AudioClipCache::ValidateFile(Utf8(destination));
	FP_EXPECT(loaded && loaded.Clip->IsStreaming());
	if (!loaded) return;
	fs::resize_file(destination, fs::file_size(destination) / 2);
	AudioResult result;
	FP_EXPECT(!AudioFileStream::Open(*loaded.Clip, false, result));
	FP_EXPECT(!result);
	FP_EXPECT(!AudioClipCache::ValidateFile(Utf8(destination)));
	fs::remove(destination);
	FP_EXPECT(!AudioFileStream::Open(*loaded.Clip, false, result));
	FP_EXPECT(result.Code == AudioError::MissingClip);
}

FP_TEST(AudioFileStreaming_DevicePauseResumeReplaceStopAndShutdown)
{
	Engine::RegisterEngineComponents();
	Fixture fixture;
	fixture.Mp3();
	fixture.Wav("Ambience.wav", 1);
	const char* previous = SDL_GetHint(SDL_HINT_AUDIO_DRIVER); const String saved = previous ? previous : "";
	SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
	{
		AudioDevice device; FP_EXPECT(device.Initialize());
		StrongObjectPtr<World> world(NewObject<World>()); world->GetAudio().Attach(&device);
		world->Add<AudioListenerComponent>(world->CreateEntity("Listener"));
		const Entity emitter = world->CreateEntity("Streamed ambience");
		auto& source = world->Add<AudioSourceComponent>(emitter);
		source.Clip = fixture.Relative("Ambience.wav"); source.Spatial = true; source.Loop = true; source.PlayOnStart = true;
		world->BeginPlay();
		const auto first = world->GetAudio().GetSourceVoice(emitter); FP_EXPECT(first);
		FP_EXPECT(world->GetAudio().Pause(first)); SDL_Delay(15); FP_EXPECT(world->GetAudio().GetState(first) == VoiceState::Paused);
		FP_EXPECT(world->GetAudio().Resume(first)); FP_EXPECT(world->GetAudio().GetState(first) == VoiceState::Playing);
		const auto replacement = world->GetAudio().Play(emitter); FP_EXPECT(replacement && replacement.Voice != first);
		FP_EXPECT(world->GetAudio().GetState(first) == VoiceState::Invalid);
		const auto music = device.GetClipCache().Load(fixture.Relative("Long.mp3")); FP_EXPECT(music);
		AudioPlaybackParams params; params.Bus = AudioBus::Music; params.Loop = true;
		const auto playing = device.Play(123, music.Clip, params); FP_EXPECT(playing);
		params.Pitch = 2; FP_EXPECT(device.SetParameters(playing.Voice, params));
		FP_EXPECT(device.Stop(playing.Voice)); FP_EXPECT(device.GetState(playing.Voice) == VoiceState::Invalid);
		world->EndPlay(); world->GetAudio().Attach(nullptr);
		FP_EXPECT(device.GetVoices().IsEmpty());
		device.Shutdown(); FP_EXPECT(!device.IsReady());
	}
	if (saved.IsEmpty()) SDL_ResetHint(SDL_HINT_AUDIO_DRIVER); else SDL_SetHint(SDL_HINT_AUDIO_DRIVER, saved.CStr());
}

FP_TEST(AudioFileStreaming_StereoVbrMp3LoopsMatchCachedSamples)
{
	Fixture fixture;
	const auto path = fixture.Directory / "Stereo.mp3";
	fs::copy_file(Native(Paths::GetEngineDir()).parent_path() / "src/Tests/Fixtures/Audio/StereoVbr.mp3", path);
	const auto cached = AudioClipCache::ValidateFile(Utf8(path)); FP_EXPECT(cached);
	if (!cached) return;
	AudioClip clip = *cached.Clip; clip.StreamPath = Utf8(path);
	AudioResult result; auto stream = AudioFileStream::Open(clip, true, result); FP_EXPECT(stream && result);
	if (!stream) return;
	std::array<float, 1024> samples{};
	uint64 frames = 0;
	const uint64 deadline = SDL_GetTicks() + 5000;
	while (frames < clip.Frames * 3 && !stream->Failed() && SDL_GetTicks() < deadline)
	{
		const uint32 count = stream->Read(samples.data(), uint32(std::min<uint64>(512, clip.Frames * 3 - frames)));
		for (uint32 frame = 0; frame < count; ++frame)
			for (int channel = 0; channel < 2; ++channel)
				FP_EXPECT(samples[frame * 2 + channel] == clip.Samples[((frames + frame) % clip.Frames) * 2 + channel]);
		frames += count;
		if (!count) SDL_Delay(1);
	}
	FP_EXPECT(frames == clip.Frames * 3 && !stream->Failed());
}

FP_TEST(AudioFileStreaming_TruncationDuringPlaybackReportsWorkerFailure)
{
	Fixture fixture;
	const auto path = fixture.Wav("Interrupted.wav");
	const auto loaded = AudioClipCache::ValidateFile(Utf8(path)); FP_EXPECT(loaded);
	if (!loaded) return;
	AudioResult result; auto stream = AudioFileStream::Open(*loaded.Clip, false, result); FP_EXPECT(stream && result);
	if (!stream) return;
	fs::resize_file(path, 44 + 16);
	std::array<float, 1024> samples{};
	const uint64 deadline = SDL_GetTicks() + 3000;
	while (!stream->Failed() && SDL_GetTicks() < deadline)
	{
		if (!stream->Read(samples.data(), 512)) SDL_Delay(1);
	}
	FP_EXPECT(stream->Failed());
}
