#include "Tests/TestFramework.h"
#include "Engine/Engine.h"
#include "Engine/Audio/AudioDevice.h"
#include "Engine/Paths.h"
#include "Engine/SceneSerializer.h"
#include "Engine/Scripting/ScriptRuntime.h"
#include "Engine/Scripting/ScriptSystem.h"

#include <SDL3/SDL.h>

using namespace FP;

FP_TEST(AudioPlayground_SceneHasMusicDopplerClapsAndThreeRooms)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	FP_EXPECT(SceneSerializer::LoadFromFile(*world.Get(), Paths::Content("Scenes/AudioPlayground.scene").CStr()));
	const Entity music = world->FindEntityByName("MP3 Music (M)");
	const Entity doppler = world->FindEntityByName("MP3 Doppler (5)");
	FP_EXPECT(world->IsValid(music) && world->IsValid(doppler));
	if (!world->IsValid(music) || !world->IsValid(doppler)) return;
	const auto& track = world->Get<AudioSourceComponent>(music);
	const auto& moving = world->Get<AudioSourceComponent>(doppler);
	FP_EXPECT(!track.Enabled && !track.Spatial && track.Loop && track.Bus == "Music");
	FP_EXPECT(!moving.Enabled && moving.Spatial && moving.Loop && moving.DopplerScale == 1);
	const auto longMp3 = AudioClipCache::ValidateFile(Paths::Content(track.Clip.View()));
	const auto monoMp3 = AudioClipCache::ValidateFile(Paths::Content(moving.Clip.View()));
	FP_EXPECT(longMp3 && longMp3.Clip->IsStreaming() && longMp3.Clip->Channels == 2 && longMp3.Clip->Duration > 60);
	FP_EXPECT(monoMp3 && monoMp3.Clip->Channels == 1 && monoMp3.Clip->Encoding == "MP3");
	int claps = 0, rooms = 0, listeners = 0;
	for (auto [entity, source] : world->View<AudioSourceComponent>().each()) if (!source.Loop) ++claps;
	for (auto [entity, room] : world->View<AudioRoomComponent>().each()) ++rooms;
	for (auto [entity, listener] : world->View<AudioListenerComponent>().each()) ++listeners;
	FP_EXPECT(claps == 4 && rooms == 3 && listeners == 1);
}

FP_TEST(AudioPlayground_MusicKeyStartsStopsAndReportsMissingFiles)
{
	Engine::RegisterEngineComponents();
	auto& runtime = ScriptRuntime::Get();
	const bool loaded = runtime.LoadModule(ScriptRuntime::GetDefaultModulePath());
	FP_EXPECT(loaded && runtime.FindType("MusicToggle"));
	if (!loaded) return;
	const char* previous = SDL_GetHint(SDL_HINT_AUDIO_DRIVER); const String saved = previous ? previous : "";
	SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
	Input::Reset();
	{
		AudioDevice device; FP_EXPECT(device.Initialize());
		StrongObjectPtr<World> world(NewObject<World>());
		const Entity entity = world->CreateEntity("MP3 Music (M)");
		auto& source = world->Add<AudioSourceComponent>(entity);
		source.Clip = "Audio/PlaygroundMusic.mp3"; source.Enabled = false; source.Loop = true; source.Bus = "Music";
		world->Add<ScriptComponent>(entity).Scripts.Emplace("MusicToggle");
		world->GetAudio().Attach(&device); world->AddSystem<ScriptSystem>(); world->BeginPlay();
		auto press = [&]() {
			Input::SetKey(SDL_SCANCODE_M, true); world->Update(.016f);
			Input::SetKey(SDL_SCANCODE_M, false); Input::EndFrame();
		};
		press();
		const auto voice = world->GetAudio().GetSourceVoice(entity);
		FP_EXPECT(source.Enabled && voice && world->GetAudio().GetState(voice) == VoiceState::Playing);
		FP_EXPECT(world->GetAudio().Pause(voice));
		press();
		FP_EXPECT(!source.Enabled && world->GetAudio().GetState(voice) == VoiceState::Invalid);
		source.Clip = "Audio/DoesNotExist.mp3";
		press();
		FP_EXPECT(!source.Enabled && !world->GetAudio().GetSourceVoice(entity));
		source.Clip.Clear(); press(); FP_EXPECT(!source.Enabled);
		world->EndPlay(); world->GetAudio().Attach(nullptr);
	}
	Input::Reset(); runtime.UnloadModule();
	if (saved.IsEmpty()) SDL_ResetHint(SDL_HINT_AUDIO_DRIVER); else SDL_SetHint(SDL_HINT_AUDIO_DRIVER, saved.CStr());
}
