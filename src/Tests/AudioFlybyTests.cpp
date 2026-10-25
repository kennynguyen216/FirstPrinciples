#include "Tests/TestFramework.h"
#include "Engine/Engine.h"
#include "Engine/Audio/AudioDevice.h"
#include "Engine/Paths.h"
#include "Engine/SceneSerializer.h"
#include "Engine/Scripting/ScriptRuntime.h"
#include "Engine/Scripting/ScriptSystem.h"

#include <SDL3/SDL.h>
#include <cmath>

using namespace FP;

FP_TEST(AudioFlyby_SceneHasOneCameraAndAnAudibleLongRangeSource)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	FP_EXPECT(SceneSerializer::LoadFromFile(*world.Get(), Paths::Content("Scenes/DopplerFlyby.scene").CStr()));
	const Entity emitter = world->FindEntityByName("Long Doppler Emitter - assign mono MP3");
	FP_EXPECT(world->IsValid(emitter));
	if (!world->IsValid(emitter)) return;
	const auto& source = world->Get<AudioSourceComponent>(emitter);
	FP_EXPECT(source.Spatial && source.Loop && !source.Occlusion && source.MaxDistance >= 500);
	const auto clip = AudioClipCache::ValidateFile(Paths::Content(source.Clip.View()));
	FP_EXPECT(clip && clip.Clip->Channels == 1);
	const auto& scripts = world->Get<ScriptComponent>(emitter).Scripts;
	FP_EXPECT(scripts.Num() == 1 && scripts[0].Name == "DopplerFlyby");
	FP_EXPECT(scripts[0].Properties.Contains("HalfDistance 250") && scripts[0].Properties.Contains("Speed 45"));
	int cameras = 0, listeners = 0;
	for (auto [entity, camera] : world->View<CameraComponent>().each()) if (camera.Primary) ++cameras;
	for (auto [entity, listener] : world->View<AudioListenerComponent>().each()) if (listener.Enabled) ++listeners;
	FP_EXPECT(cameras == 1 && listeners == 1);
	FP_EXPECT(world->GetActiveCamera() == world->FindEntityByName("Player Camera"));
}

FP_TEST(AudioFlyby_LongPassReversesContinuouslyAndControlsAffectActualPitch)
{
	Engine::RegisterEngineComponents();
	auto& runtime = ScriptRuntime::Get();
	const bool loaded = runtime.LoadModule(ScriptRuntime::GetDefaultModulePath());
	FP_EXPECT(loaded && runtime.FindType("DopplerFlyby"));
	if (!loaded) return;
	const char* hint = SDL_GetHint(SDL_HINT_AUDIO_DRIVER); const String saved = hint ? hint : "";
	SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"); Input::Reset();
	{
		AudioDevice device; FP_EXPECT(device.Initialize());
		StrongObjectPtr<World> world(NewObject<World>());
		const Entity listener = world->CreateEntity("Listener");
		world->Get<TransformComponent>(listener).Position = {0, 1.65f, 80}; world->Add<AudioListenerComponent>(listener);
		const Entity emitter = world->CreateEntity("Long emitter");
		auto& source = world->Add<AudioSourceComponent>(emitter);
		source.Clip = "Audio/PlaygroundDoppler.mp3"; source.Loop = true; source.Spatial = true;
		source.Occlusion = false; source.MinDistance = 80; source.MaxDistance = 750;
		world->Add<ScriptComponent>(emitter).Scripts.Emplace("DopplerFlyby");
		world->GetAudio().Attach(&device); world->AddSystem<ScriptSystem>(); world->BeginPlay();
		auto position = [&]() { return world->Get<TransformComponent>(emitter).Position.X; };
		auto pitch = [&]() {
			const auto handle = world->GetAudio().GetSourceVoice(emitter);
			for (const auto& voice : device.GetVoices()) if (voice.Handle == handle) return voice.Params.Pitch;
			return 0.f;
		};
		FP_EXPECT(position() == -250 && world->GetAudio().GetSourceVoice(emitter));
		float previous = position();
		for (int frame = 0; frame < 450; ++frame)
		{
			world->Update(.05f); Input::EndFrame();
			FP_EXPECT(position() >= -250 && position() <= 250);
			FP_EXPECT(std::abs(position() - previous) <= 2.251f);
			previous = position();
			if (frame == 20) FP_EXPECT(pitch() > 1.08f);
			if (frame == 180) FP_EXPECT(pitch() < .94f);
		}
		auto key = [&](Key code) {
			Input::SetKey(code, true); world->Update(.05f);
			Input::SetKey(code, false); Input::EndFrame();
		};
		key(SDL_SCANCODE_F); const float frozen = position();
		for (int i = 0; i < 20; ++i) world->Update(.05f);
		FP_EXPECT(position() == frozen && std::abs(pitch() - 1) < .00001f);
		key(SDL_SCANCODE_R); FP_EXPECT(position() == -250);
		key(SDL_SCANCODE_P); FP_EXPECT(source.DopplerScale == 0);
		for (int i = 0; i < 20; ++i) world->Update(.05f);
		FP_EXPECT(position() > -250 && pitch() == 1);
		world->EndPlay(); world->GetAudio().Attach(nullptr);
	}
	Input::Reset(); runtime.UnloadModule();
	if (saved.IsEmpty()) SDL_ResetHint(SDL_HINT_AUDIO_DRIVER); else SDL_SetHint(SDL_HINT_AUDIO_DRIVER, saved.CStr());
}
