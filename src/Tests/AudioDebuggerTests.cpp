#include "Tests/TestFramework.h"
#include "Engine/Engine.h"
#include "Engine/Material.h"
#include "Engine/Physics/PhysicsSystem.h"
#include "Engine/Audio/AudioDevice.h"
#include "Engine/SceneSerializer.h"
#include <SDL3/SDL.h>

using namespace FP;

FP_TEST(AudioDebugger_RoomClapsStayOnObjectsWhileListenerMoves)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	FP_EXPECT(SceneSerializer::LoadFromFile(*world.Get(), Paths::Content("Scenes/Acoustics3D.scene").CStr()));
	int claps = 0;
	for (auto [entity, source] : world->View<AudioSourceComponent>().each()) if (!source.Loop) ++claps;
	FP_EXPECT(claps == 4);
	const Entity emitter = world->FindEntityByName("Soft clap emitter");
	FP_EXPECT(world->IsValid(emitter));
	if (!world->IsValid(emitter)) return;
	const Vec3 fixed = world->GetWorldPosition(emitter);
	const Entity listener = world->CreateEntity("Test listener");
	for (auto [entity, component] : world->View<AudioListenerComponent>().each()) component.Enabled = false;
	world->Add<AudioListenerComponent>(listener);
	world->AddSystem<PhysicsSystem>(); world->BeginPlay();
	OneShotParams params; params.Spatial = true; params.Owner = emitter;
	params.MinDistance = 3; params.MaxDistance = 30;
	world->Get<TransformComponent>(listener).Position = fixed + Vec3{-5, 0.15f, 0};
	const auto right = world->GetAudio().GetSpatialParameters(params);
	world->Get<TransformComponent>(listener).Position = fixed + Vec3{5, 0.15f, 0};
	const auto left = world->GetAudio().GetSpatialParameters(params);
	FP_EXPECT(right.RightGain > right.LeftGain * 10 && left.LeftGain > left.RightGain * 10);
	world->Get<TransformComponent>(listener).Position = fixed + Vec3{0, 0.15f, 2};
	const auto close = world->GetAudio().GetSpatialParameters(params);
	world->Get<TransformComponent>(listener).Position = fixed + Vec3{0, 0.15f, 5};
	const auto far = world->GetAudio().GetSpatialParameters(params);
	FP_EXPECT(close.LeftGain > far.LeftGain && close.RightGain > far.RightGain);
	FP_EXPECT(world->GetWorldPosition(emitter) == fixed);
	world->GetAudio().SetDebugEnabled(true);
	world->GetAudio().GetSpatialParameters(params);
	const auto& debug = world->GetAudio().GetDebugSources();
	FP_EXPECT(debug.size() == 1 && debug[0].Owner == emitter && debug[0].Position == fixed);
	world->EndPlay();
}

FP_TEST(AudioDebugger_OptInCapturesActualPathsMaterialsAndAppliedGains)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	const Entity listener = world->CreateEntity("Listener"); world->Add<AudioListenerComponent>(listener);
	const Entity source = world->CreateEntity("Source"); world->Get<TransformComponent>(source).Position = {8, 0, 0};
	const Entity wall = world->CreateEntity("Wall"); world->Get<TransformComponent>(wall).Position = {4, 0, 0};
	world->Add<ColliderComponent>(wall).Size = {0.25f, 4, 4};
	world->Add<MaterialComponent>(wall).Material = "Materials/Acoustics/SoftWall.material";
	world->AddSystem<PhysicsSystem>(); world->BeginPlay();
	OneShotParams params; params.Owner = source; params.Spatial = true;
	world->GetAudio().GetSpatialParameters(params);
	FP_EXPECT(world->GetAudio().GetDebugSources().empty());
	world->GetAudio().SetDebugEnabled(true);
	const auto applied = world->GetAudio().GetSpatialParameters(params);
	const auto& captured = world->GetAudio().GetDebugSources();
	FP_EXPECT(captured.size() == 1);
	if (!captured.empty())
	{
		FP_EXPECT(captured[0].Owner == source && captured[0].Rays.size() == 1 && captured[0].Hits.size() == 1);
		FP_EXPECT(captured[0].Params.RightGain == applied.RightGain && captured[0].Params.LowPassHz == applied.LowPassHz);
		FP_EXPECT(captured[0].Rays[0].To == Vec3(8, 0, 0));
		if (!captured[0].Hits.empty())
		{
			const auto& hit = captured[0].Hits[0];
			FP_EXPECT(hit.Object == wall && hit.Material == "Materials/Acoustics/SoftWall.material");
			FP_EXPECT(hit.Transmission == 0.05f && hit.Absorption == 0.85f && hit.Damping == 0.9f);
			FP_EXPECT(captured[0].Transmission == hit.Transmission);
		}
	}
	world->GetAudio().SetDebugEnabled(false);
	FP_EXPECT(world->GetAudio().GetDebugSources().empty());
	world->GetAudio().SetDebugEnabled(true);
	for (int i = 0; i < 80; ++i)
	{
		params.Owner = world->CreateEntity("Bounded source");
		world->GetAudio().GetSpatialParameters(params);
	}
	FP_EXPECT(world->GetAudio().GetDebugSources().size() == 64);
	world->EndPlay();
	FP_EXPECT(world->GetAudio().GetDebugSources().empty());
	const char* hint = SDL_GetHint(SDL_HINT_AUDIO_DRIVER);
	const String saved = hint ? hint : "";
	SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
	{
		AudioDevice device; FP_EXPECT(device.Initialize());
		FP_EXPECT(world->GetAudio().Attach(&device));
		world->BeginPlay();
		OneShotParams shot; shot.Spatial = true;
		const auto voice = world->GetAudio().PlayOneShot("Audio/Clap.wav", shot);
		FP_EXPECT(voice);
		const auto& bound = world->GetAudio().GetDebugSources();
		FP_EXPECT(bound.size() == 1 && bound[0].Key == voice.Voice.Generation);
		world->EndPlay(); world->GetAudio().Attach(nullptr);
	}
	if (saved.IsEmpty()) SDL_ResetHint(SDL_HINT_AUDIO_DRIVER); else SDL_SetHint(SDL_HINT_AUDIO_DRIVER, saved.CStr());
}
