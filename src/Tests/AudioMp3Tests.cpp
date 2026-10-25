#include "Tests/TestFramework.h"
#include "Editor/AudioImport.h"
#include "Engine/Engine.h"
#include "Engine/Audio/AudioDevice.h"
#include "Engine/Paths.h"

#include <SDL3/SDL.h>
#include <cmath>
#include <filesystem>
#include <fstream>

using namespace FP;

namespace
{
	namespace fs = std::filesystem;
	String Utf8(const fs::path& path)
	{
		const auto text = path.generic_u8string();
		return {reinterpret_cast<const char*>(text.data()), int32(text.size())};
	}
	fs::path Native(const String& path) { return fs::path(reinterpret_cast<const char8_t*>(path.CStr())); }
	struct Mp3Fixture
	{
		fs::path Directory = Native(Paths::Content("Audio")) / Guid::New().ToString().CStr();
		Mp3Fixture() { fs::create_directories(Directory); }
		~Mp3Fixture() { std::error_code ec; fs::remove_all(Directory, ec); }
		fs::path Source(const char* name) const
		{
			return Native(Paths::GetEngineDir()).parent_path() / "src/Tests/Fixtures/Audio" / name;
		}
		fs::path Copy(const char* source, const fs::path& name)
		{
			const auto path = Directory / name;
			fs::copy_file(Source(source), path);
			return path;
		}
		String Relative(const char* name) const { return Utf8(fs::path("Audio") / Directory.filename() / name); }
	};
}

// Fixtures are original FFmpeg sine tones: mono CBR 44.1 kHz, stereo VBR 48 kHz,
// untagged CBR 22.05 kHz, and 61 seconds of 8 kHz silence. Tests need no external decoder.
FP_TEST(AudioMp3_FormatsMetadataUnicodeAndSignal)
{
	Mp3Fixture fixture;
	struct Format { const char* File; int Channels, Rate; double Minimum, Maximum; };
	for (const Format format : {
		Format{"MonoCbr.mp3", 1, 44100, .249, .251},
		Format{"StereoVbr.mp3", 2, 48000, .249, .251},
		Format{"MonoNoTags.mp3", 1, 22050, .25, .35}})
	{
		const auto decoded = AudioClipCache::ValidateFile(Utf8(fixture.Source(format.File)));
		FP_EXPECT(decoded);
		if (!decoded) continue;
		const auto& clip = *decoded.Clip;
		FP_EXPECT(clip.Encoding == "MP3" && clip.Channels == format.Channels && clip.SampleRate == format.Rate);
		FP_EXPECT(clip.Duration >= format.Minimum && clip.Duration <= format.Maximum);
		FP_EXPECT(clip.Frames * clip.Channels == clip.Samples.size());
		FP_EXPECT(clip.SourceBytes == fs::file_size(fixture.Source(format.File)));
		double energy = 0, difference = 0;
		for (float sample : clip.Samples) { FP_EXPECT(std::isfinite(sample) && std::abs(sample) <= 1); energy += sample * sample; }
		FP_EXPECT(energy / clip.Samples.size() > .001);
		if (clip.Channels == 2)
		{
			for (size_t i = 0; i < clip.Samples.size(); i += 2) difference += std::abs(clip.Samples[i] - clip.Samples[i + 1]);
			FP_EXPECT(difference / clip.Frames > .01);
		}
	}
	const auto unicode = fixture.Copy("MonoCbr.mp3", u8"音声 café.MP3");
	AudioClipCache cache(Utf8(fixture.Directory));
	const auto first = cache.Load(Utf8(unicode.filename()));
	FP_EXPECT(first);
	FP_EXPECT(cache.Load(Utf8(unicode.filename())).Clip == first.Clip);
	if (first) FP_EXPECT(cache.ResidentBytes() == first.Clip->Samples.size() * sizeof(float));
	FP_EXPECT(AudioClipCache::ValidateFile(Utf8(fixture.Copy("MonoCbr.mp3", "snapshot.tmp"))));
}

FP_TEST(AudioMp3_RejectsCorruptTruncatedAndOverBudget)
{
	Mp3Fixture fixture;
	const auto corrupt = fixture.Directory / "Corrupt.mp3";
	{ std::ofstream file(corrupt, std::ios::binary); file << "ID3\4\0\0\0\0\0\0not audio data"; }
	FP_EXPECT(!AudioClipCache::ValidateFile(Utf8(corrupt)));
	const auto truncated = fixture.Copy("MonoCbr.mp3", "Truncated.mp3");
	fs::resize_file(truncated, fs::file_size(truncated) / 2);
	FP_EXPECT(!AudioClipCache::ValidateFile(Utf8(truncated)));
	const auto changing = fixture.Directory / "Changing.mp3";
	{
		std::ofstream file(changing, std::ios::binary);
		for (const char* name : {"MonoNoTags.mp3", "StereoVbr.mp3"})
		{
			std::ifstream input(fixture.Source(name), std::ios::binary);
			file << input.rdbuf();
		}
	}
	FP_EXPECT(!AudioClipCache::ValidateFile(Utf8(changing)));
	const auto longClip = AudioClipCache::ValidateFile(Utf8(fixture.Source("TooLong.mp3")));
	FP_EXPECT(longClip && longClip.Clip->IsStreaming() && longClip.Clip->Samples.empty());
	fixture.Copy("MonoCbr.mp3", "Tone.mp3");
	AudioClipCache resident(Utf8(fixture.Directory), 1);
	FP_EXPECT(resident.Load("Tone.mp3").Result.Code == AudioError::BudgetExceeded);
	AudioClipCache transient(Utf8(fixture.Directory), 128ull * 1024 * 1024, 1024 * 1024 + fs::file_size(fixture.Directory / "Tone.mp3") + 4);
	FP_EXPECT(transient.Load("Tone.mp3").Result.Code == AudioError::BudgetExceeded);
}

FP_TEST(AudioMp3_ImportPreservesFormatAndNeverOverwrites)
{
	Mp3Fixture fixture;
	const auto destination = fixture.Directory / fs::path(u8"Tone 音.MP3");
	String relative = "unchanged";
	FP_EXPECT(Editor::ImportAudioFile(Utf8(fixture.Source("MonoCbr.mp3")), Utf8(destination), relative));
	FP_EXPECT(relative.EndsWith("Tone 音.MP3"));
	FP_EXPECT(fs::file_size(destination) == fs::file_size(fixture.Source("MonoCbr.mp3")));
	FP_EXPECT(Editor::ImportAudioFile(Utf8(destination), Utf8(destination), relative));
	relative = "unchanged";
	FP_EXPECT(!Editor::ImportAudioFile(Utf8(fixture.Source("StereoVbr.mp3")), Utf8(destination), relative));
	FP_EXPECT(!Editor::ImportAudioFile(Utf8(fixture.Source("MonoCbr.mp3")), Utf8(fixture.Directory / "Wrong.wav"), relative));
	FP_EXPECT(!fs::exists(fixture.Directory / "Wrong.wav") && relative == "unchanged");
	const auto mislabeled = fixture.Copy("MonoCbr.mp3", "Wrong.wav");
	FP_EXPECT(!Editor::ImportAudioFile(Utf8(mislabeled), Utf8(mislabeled), relative));
	const auto corrupt = fixture.Directory / "Corrupt.mp3";
	{ std::ofstream file(corrupt); file << "This is not an MP3 file"; }
	FP_EXPECT(!Editor::ImportAudioFile(Utf8(corrupt), Utf8(fixture.Directory / "Failed.mp3"), relative));
	FP_EXPECT(!fs::exists(fixture.Directory / "Failed.mp3") && relative == "unchanged");
	for (const auto& entry : fs::directory_iterator(fixture.Directory))
		FP_EXPECT(!Utf8(entry.path().filename()).StartsWith(".audio-import-"));
}

FP_TEST(AudioMp3_StereoSourcesAutomaticallyMixToMonoForSpatialPlayback)
{
	Engine::RegisterEngineComponents();
	const char* previous = SDL_GetHint(SDL_HINT_AUDIO_DRIVER); const String saved = previous ? previous : "";
	SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
	{
		Mp3Fixture fixture;
		fixture.Copy("MonoCbr.mp3", "Mono.mp3"); fixture.Copy("StereoVbr.mp3", "Stereo.mp3");
		AudioDevice device; FP_EXPECT(device.Initialize());
		StrongObjectPtr<World> world(NewObject<World>()); world->GetAudio().Attach(&device);
		world->Add<AudioListenerComponent>(world->CreateEntity("Listener"));
		const Entity emitter = world->CreateEntity("MP3 source"); world->Get<TransformComponent>(emitter).Position = {10, 0, 0};
		auto& source = world->Add<AudioSourceComponent>(emitter);
		source.Clip = fixture.Relative("Stereo.mp3"); source.Spatial = true; source.Loop = true; source.PlayOnStart = true;
		world->BeginPlay();
		const VoiceHandle handle = world->GetAudio().GetSourceVoice(emitter); FP_EXPECT(handle);
		FP_EXPECT(device.GetState(handle) == VoiceState::Playing);
		for (int i = 0; i < 6; ++i) { world->Get<TransformComponent>(emitter).Position.X -= .5f; world->Update(.05f); }
		bool shifted = false;
		for (const auto& voice : device.GetVoices()) if (voice.Handle == handle) shifted = voice.Params.Pitch > 1.02f;
		FP_EXPECT(shifted);
		FP_EXPECT(source.Clip == fixture.Relative("Stereo.mp3"));
		const auto stereo = device.GetClipCache().Load(source.Clip);
		const auto mono = device.GetClipCache().Load(source.Clip, false, true);
		FP_EXPECT(stereo && stereo.Clip->Channels == 2 && mono && mono.Clip->Channels == 1);
		if (stereo && mono)
			for (size_t i = 0; i < mono.Clip->Samples.size(); ++i)
				FP_EXPECT(mono.Clip->Samples[i] == (stereo.Clip->Samples[i * 2] + stereo.Clip->Samples[i * 2 + 1]) * .5f);
		FP_EXPECT(world->GetAudio().Pause(handle));
		source.Spatial = false; world->Update(.016f);
		const auto unpositioned = world->GetAudio().GetSourceVoice(emitter);
		FP_EXPECT(unpositioned && unpositioned != handle && device.GetState(unpositioned) == VoiceState::Paused);
		source.Spatial = true; world->Update(.016f);
		const auto positioned = world->GetAudio().GetSourceVoice(emitter);
		FP_EXPECT(positioned && positioned != unpositioned && device.GetState(positioned) == VoiceState::Paused);
		OneShotParams shot; shot.Spatial = true;
		FP_EXPECT(world->GetAudio().PlayOneShot(fixture.Relative("Stereo.mp3"), shot));
		shot.Spatial = false; FP_EXPECT(world->GetAudio().PlayOneShot(fixture.Relative("Stereo.mp3"), shot));
		world->EndPlay(); world->GetAudio().Attach(nullptr);
	}
	if (saved.IsEmpty()) SDL_ResetHint(SDL_HINT_AUDIO_DRIVER); else SDL_SetHint(SDL_HINT_AUDIO_DRIVER, saved.CStr());
}
