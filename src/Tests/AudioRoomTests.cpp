#include "Tests/TestFramework.h"
#include "Engine/Engine.h"
#include "Engine/Audio/AudioDevice.h"
#include "Engine/Audio/AudioRoomEffect.h"
#include "Engine/Physics/PhysicsSystem.h"
#include "Engine/SceneSerializer.h"

#include <SDL3/SDL.h>
#include <cmath>
#include <limits>

using namespace FP;
namespace FP { bool AudioRoomOfflineSelfCheck(); }

FP_TEST(AudioRoom_BoxPathsRequireSolidUnobstructedSurfaces)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	const Entity listener = world->CreateEntity("Listener");
	world->Add<AudioListenerComponent>(listener);
	const Entity room = world->CreateEntity("Room");
	auto& settings = world->Add<AudioRoomComponent>(room);
	settings.Size = {8, 4, 10}; settings.Wet = 0.8f;
	Entity walls[6];
	for (int i = 0; i < 6; ++i)
	{
		walls[i] = world->CreateEntity("Wall");
		Vec3 size{8.4f, 4.4f, 10.4f}, position;
		const float extents[] = {4, 2, 5};
		const float offset = (extents[i / 2] + 0.1f) * (i % 2 ? 1.f : -1.f);
		if (i / 2 == 0) { size.X = 0.2f; position.X = offset; }
		if (i / 2 == 1) { size.Y = 0.2f; position.Y = offset; }
		if (i / 2 == 2) { size.Z = 0.2f; position.Z = offset; }
		world->Get<TransformComponent>(walls[i]).Position = position;
		world->Add<ColliderComponent>(walls[i]).Size = size;
	}
	world->AddSystem<PhysicsSystem>();
	world->BeginPlay();
	OneShotParams params; params.Spatial = true; params.MaxDistance = 100;
	const auto wet = world->GetAudio().GetSpatialParameters(params);
	FP_EXPECT(wet.Room.Wet == 0.8f && wet.Room.LateGain > 0);
	const float lengths[] = {8, 8, 4, 4, 10, 10};
	for (size_t i = 0; i < 6; ++i)
	{
		FP_EXPECT(std::abs(wet.Room.Reflections[i].Delay - lengths[i] / 343.f) < 0.00001f);
		FP_EXPECT(wet.Room.Reflections[i].LeftGain + wet.Room.Reflections[i].RightGain > 0);
	}
	world->Get<ColliderComponent>(walls[0]).Trigger = true;
	world->Update(0);
	const auto missing = world->GetAudio().GetSpatialParameters(params);
	FP_EXPECT(missing.Room.Reflections[0].LeftGain == 0 && missing.Room.Reflections[0].RightGain == 0);
	FP_EXPECT(missing.Room.Reflections[1].RightGain > 0);
	params.Position = {20, 0, 0};
	FP_EXPECT(world->GetAudio().GetSpatialParameters(params).Room.Wet == 0);
	params.Position = {}; params.Spatial = false;
	FP_EXPECT(world->GetAudio().GetSpatialParameters(params).Room.Wet == 0);
	params.Spatial = true;
	world->Get<TransformComponent>(listener).Position = {20, 0, 0};
	FP_EXPECT(world->GetAudio().GetSpatialParameters(params).Room.Wet == 0);
	world->Get<TransformComponent>(listener).Position = {};
	settings.Decay = std::numeric_limits<float>::quiet_NaN();
	FP_EXPECT(world->GetAudio().GetSpatialParameters(params).Room.Wet == 0);
	settings.Decay = 1;
	world->Get<TransformComponent>(room).Position = {20, 0, 10};
	world->Get<TransformComponent>(room).Rotation = {0, 37, 0};
	FP_EXPECT(world->SetParent(listener, room, false));
	for (Entity wall : walls) FP_EXPECT(world->SetParent(wall, room, false));
	params.Position = {20, 0, 10};
	world->Update(0);
	const auto rotated = world->GetAudio().GetSpatialParameters(params);
	FP_EXPECT(rotated.Room.Wet == 0.8f);
	FP_EXPECT(std::abs(rotated.Room.Reflections[1].Delay - 8.f / 343) < 0.00001f);
	FP_EXPECT(rotated.Room.Reflections[1].RightGain > 0);
	const Entity blocker = world->CreateEntity("Blocked reflection leg");
	world->Get<TransformComponent>(blocker).Position = {2, 0, 0};
	world->Add<ColliderComponent>(blocker).Size = {0.2f, 0.5f, 0.5f};
	FP_EXPECT(world->SetParent(blocker, room, false));
	world->Update(0);
	const auto obstructed = world->GetAudio().GetSpatialParameters(params);
	FP_EXPECT(std::abs(obstructed.Room.Reflections[1].RightGain - rotated.Room.Reflections[1].RightGain * 0.0625f) < 0.00001f);
	world->EndPlay();
}

FP_TEST(AudioRoom_ImpulseDelayStereoDecayAndDamping)
{
	AudioRoomParams params;
	params.Decay = 1; params.Damping = 0; params.Reflections[0] = {0.1f, 0.5f, 0.25f};
	AudioRoomEffect effect(48000);
	effect.Configure(params);
	float peak = 0;
	for (int i = 0; i < 6000; ++i)
	{
		const auto sample = effect.Process(i == 0 ? 1.f : 0.f);
		if (i < 4800) FP_EXPECT(sample.X == 0 && sample.Y == 0);
		if (i == 4800) { peak = sample.X; FP_EXPECT(peak > 0.1f && std::abs(sample.X - sample.Y * 2) < 0.00001f); }
	}
	params.Damping = 0.95f;
	params.Reflections[0].Damping = 0.95f;
	AudioRoomEffect damped(48000); damped.Configure(params);
	float dampedPeak = 0;
	for (int i = 0; i < 6000; ++i) dampedPeak = std::max(dampedPeak, std::abs(damped.Process(i == 0 ? 1.f : 0.f).X));
	FP_EXPECT(dampedPeak < peak * 0.2f);
	params.Reflections = {}; params.Damping = 0.1f; params.LateGain = 0.6f;
	params.Decay = 0.2f; AudioRoomEffect shortRoom(48000); shortRoom.Configure(params);
	params.Decay = 3; AudioRoomEffect hall(48000); hall.Configure(params);
	double shortEnergy = 0, hallEnergy = 0;
	for (int i = 0; i < 240000; ++i)
	{
		const auto a = shortRoom.Process(i == 0 ? 1.f : 0.f), b = hall.Process(i == 0 ? 1.f : 0.f);
		FP_EXPECT(std::isfinite(b.X) && std::isfinite(b.Y) && std::abs(b.X) < 1 && std::abs(b.Y) < 1);
		if (i > 24000) { shortEnergy += a.X * a.X + a.Y * a.Y; hallEnergy += b.X * b.X + b.Y * b.Y; }
	}
	FP_EXPECT(hallEnergy > shortEnergy * 100 && hallEnergy > 0.000001);
}

FP_TEST(AudioRoom_ClipTailMixerMuteAndInvalidParameters)
{
	FP_EXPECT(AudioRoomOfflineSelfCheck());
	SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
	{
		AudioDevice device; FP_EXPECT(device.Initialize());
		AudioPlaybackParams params; params.Room.Wet = 0.8f;
		params.Room.Decay = 100;
		FP_EXPECT(!device.CreateStream(1, 48000, params));
		params.Room.Decay = 1; params.Room.Reflections[0].Delay = std::numeric_limits<float>::infinity();
		FP_EXPECT(!device.CreateStream(1, 48000, params));
		params.Room.Reflections[0] = {0.1f, 0.5f, 0.5f};
		const auto stream = device.CreateStream(1, 48000, params);
		FP_EXPECT(stream);
		if (stream) { device.StopContext(1); FP_EXPECT(device.GetState(stream.Voice) == VoiceState::Invalid); }
	}
	SDL_ResetHint(SDL_HINT_AUDIO_DRIVER);
}

FP_TEST(AudioRoom_SettingsRoundTrip)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	const Entity room = world->CreateEntity("Acoustic room");
	auto& settings = world->Add<AudioRoomComponent>(room);
	settings.Size = {12, 5, 14}; settings.Decay = 1.6f; settings.Wet = 0.85f;
	const auto saved = SceneSerializer::SaveToString(*world.Get());
	StrongObjectPtr<World> loaded(NewObject<World>());
	FP_EXPECT(SceneSerializer::LoadFromString(*loaded.Get(), saved.View()));
	const auto& back = loaded->Get<AudioRoomComponent>(loaded->FindEntityByName("Acoustic room"));
	FP_EXPECT(back.Size == settings.Size && back.Wet == settings.Wet && back.Decay == settings.Decay);
}
