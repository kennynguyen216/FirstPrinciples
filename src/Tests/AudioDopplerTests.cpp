#include "Tests/TestFramework.h"
#include "Engine/Engine.h"
#include "Engine/Audio/AudioDevice.h"
#include "Engine/SceneSerializer.h"

#include <SDL3/SDL.h>
#include <cmath>
#include <limits>

using namespace FP;

FP_TEST(AudioDoppler_ApproachRecedeListenerMotionAndSafeBounds)
{
	const Vec3 position{10, 0, 0};
	FP_EXPECT(CalculateAudioDoppler(position, {}, {}, {}) == 1);
	FP_EXPECT(std::abs(CalculateAudioDoppler(position, {}, {-30, 0, 0}, {}) - 343.f / 313) < 0.000001f);
	FP_EXPECT(std::abs(CalculateAudioDoppler(position, {}, {30, 0, 0}, {}) - 343.f / 373) < 0.000001f);
	FP_EXPECT(std::abs(CalculateAudioDoppler(position, {}, {}, {20, 0, 0}) - 363.f / 343) < 0.000001f);
	FP_EXPECT(CalculateAudioDoppler(position, {}, {20, 0, 0}, {20, 0, 0}) == 1);
	FP_EXPECT(CalculateAudioDoppler(position, {}, {0, 30, 0}, {}) == 1);
	FP_EXPECT(CalculateAudioDoppler({}, {}, {30, 0, 0}, {}) == 1);
	FP_EXPECT(CalculateAudioDoppler(position, {}, {-30, 0, 0}, {}, 0) == 1);
	FP_EXPECT(CalculateAudioDoppler(position, {}, {-10000, 0, 0}, {10000, 0, 0}) == 2);
	FP_EXPECT(CalculateAudioDoppler(position, {}, {10000, 0, 0}, {-10000, 0, 0}) == 0.5f);
	FP_EXPECT(CalculateAudioDoppler(position, {}, {std::numeric_limits<float>::quiet_NaN(), 0, 0}, {}) == 1);
}

FP_TEST(AudioDoppler_WorldMotionParentsCameraChangesAndTeleports)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	const Entity listener = world->CreateEntity("Listener"); world->Add<AudioListenerComponent>(listener);
	const Entity parent = world->CreateEntity("Moving parent"); world->Get<TransformComponent>(parent).Position = {6, 0, 0};
	const Entity source = world->CreateEntity("Source"); world->Get<TransformComponent>(source).Position = {4, 0, 0};
	FP_EXPECT(world->SetParent(source, parent, false));
	world->BeginPlay();
	OneShotParams params; params.Owner = source; params.Spatial = true; params.MaxDistance = 1000;
	FP_EXPECT(world->GetAudio().GetSpatialParameters(params).Pitch == 1);
	world->Get<TransformComponent>(parent).Position.X -= 3; world->Update(0.1f);
	const auto approaching = world->GetAudio().GetSpatialParameters(params);
	FP_EXPECT(std::abs(approaching.Pitch - 343.f / 313) < 0.00001f);
	FP_EXPECT(world->GetAudio().GetSpatialParameters(params).Pitch == approaching.Pitch);
	world->Get<TransformComponent>(parent).Position.X += 3; world->Update(0.1f);
	FP_EXPECT(std::abs(world->GetAudio().GetSpatialParameters(params).Pitch - 343.f / 373) < 0.00001f);
	world->Get<TransformComponent>(listener).Position.X += 2; world->Update(0.1f);
	FP_EXPECT(std::abs(world->GetAudio().GetSpatialParameters(params).Pitch - 363.f / 343) < 0.00001f);
	world->Get<TransformComponent>(parent).Position.X = 100; world->Update(0.1f);
	FP_EXPECT(world->GetAudio().GetSpatialParameters(params).Pitch == 1);
	const Entity next = world->CreateEntity("Next listener"); world->Get<TransformComponent>(next).Position = {90, 0, 0};
	world->Get<AudioListenerComponent>(listener).Enabled = false; world->Add<AudioListenerComponent>(next);
	world->Update(0.1f);
	FP_EXPECT(world->GetAudio().GetSpatialParameters(params).Pitch == 1);
	world->Get<TransformComponent>(parent).Position.X -= 2; world->Update(0.5f);
	FP_EXPECT(world->GetAudio().GetSpatialParameters(params).Pitch == 1);
	params.DopplerScale = 0;
	world->Get<TransformComponent>(parent).Position.X -= 1; world->Update(0.1f);
	FP_EXPECT(world->GetAudio().GetSpatialParameters(params).Pitch == 1);
	world->EndPlay();
}

FP_TEST(AudioDoppler_PlaybackSmoothsWithoutCompoundingAndCanBeDisabled)
{
	Engine::RegisterEngineComponents();
	const char* hint = SDL_GetHint(SDL_HINT_AUDIO_DRIVER); const String saved = hint ? hint : "";
	SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
	{
		AudioDevice device; FP_EXPECT(device.Initialize());
		StrongObjectPtr<World> world(NewObject<World>()); world->GetAudio().Attach(&device);
		const Entity listener = world->CreateEntity("Listener"); world->Add<AudioListenerComponent>(listener);
		const Entity emitter = world->CreateEntity("Moving emitter"); world->Get<TransformComponent>(emitter).Position = {10, 0, 0};
		auto& source = world->Add<AudioSourceComponent>(emitter);
		source.Clip = "Audio/DopplerTone.wav"; source.Spatial = true; source.Loop = true; source.PlayOnStart = true; source.Pitch = 1.2f;
		world->BeginPlay();
		const VoiceHandle handle = world->GetAudio().GetSourceVoice(emitter);
		FP_EXPECT(handle);
		auto pitch = [&](VoiceHandle voice) {
			for (const auto& info : device.GetVoices()) if (info.Handle == voice) return info.Params.Pitch;
			return 0.f;
		};
		FP_EXPECT(pitch(handle) == 1.2f);
		for (int i = 0; i < 6; ++i) { world->Get<TransformComponent>(emitter).Position.X -= 0.5f; world->Update(0.05f); }
		FP_EXPECT(std::abs(pitch(handle) - 1.2f * 343 / 333) < 0.001f);
		for (int i = 0; i < 20; ++i) world->Update(0.05f);
		FP_EXPECT(std::abs(pitch(handle) - 1.2f) < 0.00001f);
		source.DopplerScale = 0;
		world->Get<TransformComponent>(emitter).Position.X -= 0.5f; world->Update(0.05f);
		FP_EXPECT(pitch(handle) == 1.2f);
		OneShotParams shot; shot.Owner = emitter; shot.Spatial = true; shot.Pitch = 1.5f;
		const auto oneShot = world->GetAudio().PlayOneShot("Audio/DopplerTone.wav", shot); FP_EXPECT(oneShot);
		for (int i = 0; i < 4; ++i) { world->Get<TransformComponent>(emitter).Position.X -= 0.5f; world->Update(0.05f); }
		FP_EXPECT(std::abs(pitch(oneShot.Voice) - 1.5f * 343 / 333) < 0.002f);
		shot.DopplerScale = std::numeric_limits<float>::infinity();
		FP_EXPECT(world->GetAudio().PlayOneShot("Audio/DopplerTone.wav", shot).Result.Code == AudioError::InvalidSettings);
		world->EndPlay(); world->GetAudio().Attach(nullptr);
	}
	if (saved.IsEmpty()) SDL_ResetHint(SDL_HINT_AUDIO_DRIVER); else SDL_SetHint(SDL_HINT_AUDIO_DRIVER, saved.CStr());
}

FP_TEST(AudioDoppler_SourceSettingsRoundTrip)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	world->Add<AudioSourceComponent>(world->CreateEntity("Source")).DopplerScale = 0.75f;
	const String text = SceneSerializer::SaveToString(*world.Get());
	StrongObjectPtr<World> loaded(NewObject<World>());
	FP_EXPECT(SceneSerializer::LoadFromString(*loaded.Get(), text.View()));
	FP_EXPECT(loaded->Get<AudioSourceComponent>(loaded->FindEntityByName("Source")).DopplerScale == 0.75f);
}
