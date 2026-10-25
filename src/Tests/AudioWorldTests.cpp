#include "Tests/TestFramework.h"
#include "Engine/Engine.h"
#include "Engine/Audio/AudioDevice.h"
#include "Engine/SceneSerializer.h"

#include <SDL3/SDL.h>
#include <cmath>
#include <limits>

using namespace FP;

namespace
{
	Entity AddLoop(World& world)
	{
		const Entity entity = world.CreateEntity("Audio test");
		auto& source = world.Add<AudioSourceComponent>(entity);
		source.Clip = "Audio/Hum.wav";
		source.PlayOnStart = true;
		source.Loop = true;
		return entity;
	}

	class AudioLifecycleSystem : public System
	{
	public:
		Entity Owner = NullEntity;
		bool HeardStart = false;
		bool HeardStop = false;
		bool Destroy = false;
		const char* GetName() const override { return "AudioLifecycle"; }
		void OnStart(World& world) override { HeardStart = bool(world.GetAudio().GetSourceVoice(Owner)); }
		void OnStop(World& world) override { HeardStop = bool(world.GetAudio().GetSourceVoice(Owner)); }
		void Update(World& world, float) override { if (Destroy) world.DestroyEntity(Owner); }
	};

	struct DummyAudio
	{
		AudioDevice Device;
		DummyAudio()
		{
			SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
			FP_EXPECT(bool(Device.Initialize()));
		}
		~DummyAudio() { Device.Shutdown(); SDL_ResetHint(SDL_HINT_AUDIO_DRIVER); }
	};

	struct AudioOnRemoval
	{
		World* Owner;
		VoiceHandle Started;
		void OnRemoved(entt::registry&, Entity)
		{
			Started = Owner->GetAudio().PlayOneShot("Audio/Hum.wav").Voice;
		}
	};
}

FP_TEST(AudioWorld_ClearStopsSoundsStartedByDestructionCallbacks)
{
	Engine::RegisterEngineComponents();
	DummyAudio audio;
	StrongObjectPtr<World> world(NewObject<World>());
	world->GetAudio().Attach(&audio.Device);
	const Entity entity = AddLoop(*world.Get());
	world->Add<AudioListenerComponent>(entity);
	world->BeginPlay();
	AudioOnRemoval callback{ world.Get() };
	world->GetRegistry().on_destroy<AudioListenerComponent>().connect<&AudioOnRemoval::OnRemoved>(callback);
	world->Clear();
	FP_EXPECT(bool(callback.Started));
	FP_EXPECT(world->GetAudio().GetState(callback.Started) == VoiceState::Invalid);
	FP_EXPECT(world->IsPlaying());
	world->GetRegistry().on_destroy<AudioListenerComponent>().disconnect<&AudioOnRemoval::OnRemoved>(callback);
	world->EndPlay();
}

FP_TEST(AudioWorld_SpatialGains)
{
	TransformComponent listener;
	const auto center = CalculateAudioSpatialGains({}, listener, 1, 25);
	FP_EXPECT(std::abs(center.Left - std::sqrt(0.5f)) < 0.00001f);
	FP_EXPECT(center.Left == center.Right);
	const auto right = CalculateAudioSpatialGains({ 1, 0, 0 }, listener, 1, 25);
	FP_EXPECT(right.Left == 0 && right.Right == 1);
	const auto left = CalculateAudioSpatialGains({ -1, 0, 0 }, listener, 1, 25);
	FP_EXPECT(left.Left == 1 && left.Right == 0);
	const auto far = CalculateAudioSpatialGains({ 25, 0, 0 }, listener, 1, 25);
	FP_EXPECT(far.Left == 0 && far.Right == 0);
	const auto midway = CalculateAudioSpatialGains({ 2, 0, 0 }, listener, 1, 3);
	FP_EXPECT(midway.Left == 0 && std::abs(midway.Right - 0.25f) < 0.00001f);
	listener.Rotation.Y = 180;
	const auto rotated = CalculateAudioSpatialGains({ 1, 0, 0 }, listener, 1, 25);
	FP_EXPECT(std::abs(rotated.Left - 1) < 0.00001f && rotated.Right < 0.0001f);
	listener.Position.X = std::numeric_limits<float>::quiet_NaN();
	const auto invalid = CalculateAudioSpatialGains({}, listener, 1, 25);
	FP_EXPECT(invalid.Left == 0 && invalid.Right == 0);
	listener = {};
	const auto huge = CalculateAudioSpatialGains({ std::numeric_limits<float>::max(), 0, 0 }, listener, 1, 25);
	FP_EXPECT(huge.Left == 0 && huge.Right == 0);
	const auto invalidDistance = CalculateAudioSpatialGains({}, listener, 2, 1);
	FP_EXPECT(invalidDistance.Left == 0 && invalidDistance.Right == 0);
}

FP_TEST(AudioWorld_ComponentsRoundTripAndSilentCandidate)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	const Entity source = AddLoop(*world.Get());
	world->Get<AudioSourceComponent>(source).Volume = -0.5f;
	world->Get<AudioSourceComponent>(source).Bus = "InvalidBus";
	world->Add<AudioListenerComponent>(source);
	const String text = SceneSerializer::SaveToString(*world.Get());
	StrongObjectPtr<World> candidate(NewObject<World>());
	FP_EXPECT(SceneSerializer::LoadFromString(*candidate.Get(), text.View()));
	FP_EXPECT(SceneSerializer::SaveToString(*candidate.Get()) == text);
	const Entity copied = candidate->GetEntities()[0];
	FP_EXPECT(candidate->Get<AudioSourceComponent>(copied).Volume == -0.5f);
	FP_EXPECT(candidate->GetAudio().GetDevice() == nullptr);
	candidate->BeginPlay();
	FP_EXPECT(!candidate->GetAudio().GetSourceVoice(copied));
	FP_EXPECT(candidate->GetAudio().Play(copied).Result.Code == AudioError::NotReady);
	candidate->EndPlay();
	FP_EXPECT(!candidate->GetAudio().Pause({ 999, 1, 0 }));
}

FP_TEST(AudioWorld_LifecycleAndIsolation)
{
	Engine::RegisterEngineComponents();
	DummyAudio audio;
	StrongObjectPtr<World> world(NewObject<World>());
	StrongObjectPtr<World> other(NewObject<World>());
	FP_EXPECT(world->GetAudio().Attach(&audio.Device));
	FP_EXPECT(other->GetAudio().Attach(&audio.Device));
	const Entity source = AddLoop(*world.Get());
	const Entity otherSource = AddLoop(*other.Get());
	auto* system = world->AddSystem<AudioLifecycleSystem>();
	system->Owner = source;
	world->BeginPlay(); other->BeginPlay();
	FP_EXPECT(system->HeardStart);
	const VoiceHandle first = world->GetAudio().GetSourceVoice(source);
	const VoiceHandle isolated = other->GetAudio().GetSourceVoice(otherSource);
	FP_EXPECT(bool(first) && bool(isolated));
	FP_EXPECT(!other->GetAudio().Stop(first));
	FP_EXPECT(!world->GetAudio().Attach(nullptr));
	FP_EXPECT(world->GetAudio().Pause(first));
	FP_EXPECT(world->GetAudio().GetState(first) == VoiceState::Paused);
	const auto restarted = world->GetAudio().Play(source);
	FP_EXPECT(bool(restarted));
	FP_EXPECT(world->GetAudio().GetState(first) == VoiceState::Invalid);
	FP_EXPECT(world->GetAudio().GetState(restarted.Voice) == VoiceState::Playing);
	OneShotParams shot;
	shot.Owner = source;
	const auto owned = world->GetAudio().PlayOneShot("Audio/Hum.wav", shot);
	FP_EXPECT(bool(owned));
	world->Remove<AudioSourceComponent>(source);
	FP_EXPECT(world->GetAudio().GetState(restarted.Voice) == VoiceState::Invalid);
	FP_EXPECT(world->GetAudio().GetState(owned.Voice) == VoiceState::Invalid);
	auto& replacement = world->Add<AudioSourceComponent>(source);
	replacement.Clip = "Audio/Hum.wav"; replacement.Loop = true; replacement.PlayOnStart = true;
	world->Update(0.016f);
	FP_EXPECT(bool(world->GetAudio().GetSourceVoice(source)));
	system->Destroy = true;
	const VoiceHandle beforeDestroy = world->GetAudio().GetSourceVoice(source);
	world->Update(0.016f);
	FP_EXPECT(!world->IsValid(source));
	FP_EXPECT(world->GetAudio().GetState(beforeDestroy) == VoiceState::Invalid);
	world->EndPlay();
	FP_EXPECT(other->GetAudio().GetState(isolated) != VoiceState::Invalid);
	other->EndPlay();
	FP_EXPECT(other->GetAudio().GetState(isolated) == VoiceState::Invalid);
	world->GetAudio().Attach(nullptr); other->GetAudio().Attach(nullptr);
}

FP_TEST(AudioWorld_PauseHoldsOnlyGameVoices)
{
	Engine::RegisterEngineComponents();
	DummyAudio audio;
	StrongObjectPtr<World> world(NewObject<World>());
	world->GetAudio().Attach(&audio.Device);
	const Entity firstSource = AddLoop(*world.Get());
	const Entity secondSource = AddLoop(*world.Get());
	world->BeginPlay();
	const VoiceHandle first = world->GetAudio().GetSourceVoice(firstSource);
	const VoiceHandle second = world->GetAudio().GetSourceVoice(secondSource);
	FP_EXPECT(bool(first) && bool(second));
	FP_EXPECT(world->GetAudio().Pause(second));

	world->SetPaused(true);
	FP_EXPECT(world->GetAudio().GetState(first) == VoiceState::Paused);
	FP_EXPECT(world->GetAudio().GetState(second) == VoiceState::Paused);
	const auto duringPause = world->GetAudio().PlayOneShot("Audio/Hum.wav");
	FP_EXPECT(bool(duringPause));
	FP_EXPECT(world->GetAudio().GetState(duringPause.Voice) == VoiceState::Paused);

	world->SetPaused(false);
	FP_EXPECT(world->GetAudio().GetState(first) == VoiceState::Playing);
	FP_EXPECT(world->GetAudio().GetState(second) == VoiceState::Paused);
	FP_EXPECT(world->GetAudio().GetState(duringPause.Voice) == VoiceState::Playing);
	world->EndPlay();
	FP_EXPECT(world->GetAudio().GetState(first) == VoiceState::Invalid);
}

FP_TEST(AudioWorld_SceneReplacementAndListenerFallback)
{
	Engine::RegisterEngineComponents();
	DummyAudio audio;
	StrongObjectPtr<World> world(NewObject<World>());
	world->GetAudio().Attach(&audio.Device);
	const Entity source = AddLoop(*world.Get());
	const Entity camera = world->CreateEntity("Camera");
	world->Add<CameraComponent>(camera);
	const Entity listener = world->CreateEntity("Listener");
	world->Add<AudioListenerComponent>(listener);
	const Entity second = world->CreateEntity("Second listener");
	world->Add<AudioListenerComponent>(second);
	world->BeginPlay();
	FP_EXPECT(world->GetAudio().GetListenerEntity() == listener);
	world->Remove<AudioListenerComponent>(listener); world->Update(0.016f);
	FP_EXPECT(world->GetAudio().GetListenerEntity() == second);
	world->Remove<AudioListenerComponent>(second); world->Update(0.016f);
	FP_EXPECT(world->GetAudio().GetListenerEntity() == camera);
	const VoiceHandle old = world->GetAudio().GetSourceVoice(source);
	FP_EXPECT(!SceneSerializer::LoadFromString(*world.Get(), "version invalid\n"));
	FP_EXPECT(world->GetAudio().GetState(old) != VoiceState::Invalid);
	world->Get<AudioSourceComponent>(source).Clip = "Audio/DoesNotExist.wav";
	world->Update(0.016f);
	FP_EXPECT(world->GetAudio().GetState(old) != VoiceState::Invalid);
	FP_EXPECT(SceneSerializer::LoadFromString(*world.Get(), "version 2\n"));
	FP_EXPECT(world->IsPlaying());
	FP_EXPECT(world->GetAudio().GetState(old) == VoiceState::Invalid);
	const Entity fresh = AddLoop(*world.Get()); world->Update(0.016f);
	const VoiceHandle live = world->GetAudio().GetSourceVoice(fresh);
	FP_EXPECT(bool(live));
	world->Clear();
	FP_EXPECT(world->GetAudio().GetState(live) == VoiceState::Invalid);
	world->EndPlay(); world->EndPlay();
	FP_EXPECT(world->GetAudio().Attach(nullptr));
}

FP_TEST(AudioWorld_PausedReplacementAndDisable)
{
	DummyAudio audio;
	StrongObjectPtr<World> world(NewObject<World>());
	world->GetAudio().Attach(&audio.Device);
	const Entity entity = AddLoop(*world.Get());
	world->BeginPlay();
	const VoiceHandle initial = world->GetAudio().GetSourceVoice(entity);
	FP_EXPECT(world->GetAudio().Pause(initial));
	world->Get<AudioSourceComponent>(entity).Clip = "Audio/Click.wav";
	world->Update(0.016f);
	const VoiceHandle replacement = world->GetAudio().GetSourceVoice(entity);
	FP_EXPECT(bool(replacement) && replacement != initial);
	FP_EXPECT(world->GetAudio().GetState(initial) == VoiceState::Invalid);
	FP_EXPECT(world->GetAudio().GetState(replacement) == VoiceState::Paused);
	world->Get<AudioSourceComponent>(entity).Clip = "Audio/Missing.wav";
	world->Update(0.016f);
	FP_EXPECT(world->GetAudio().GetSourceVoice(entity) == replacement);
	FP_EXPECT(world->GetAudio().GetState(replacement) == VoiceState::Paused);
	world->Get<AudioSourceComponent>(entity).Volume = -1;
	FP_EXPECT(world->GetAudio().Play(entity).Result.Code == AudioError::InvalidSettings);
	FP_EXPECT(world->GetAudio().GetSourceVoice(entity) == replacement);
	OneShotParams params;
	params.Owner = entity;
	const auto owned = world->GetAudio().PlayOneShot("Audio/Hum.wav", params);
	FP_EXPECT(bool(owned));
	world->Get<AudioSourceComponent>(entity).Enabled = false;
	world->Update(0.016f);
	FP_EXPECT(world->GetAudio().GetState(replacement) == VoiceState::Invalid);
	FP_EXPECT(world->GetAudio().GetState(owned.Voice) == VoiceState::Invalid);
	world->Get<AudioSourceComponent>(entity).Enabled = true;
	world->Update(0.016f);
	FP_EXPECT(!world->GetAudio().GetSourceVoice(entity));
	world->DestroyEntity(entity); world->Update(0.016f);
	FP_EXPECT(world->GetAudio().PlayOneShot("Audio/Hum.wav", params).Result.Code == AudioError::InvalidSource);
	world->EndPlay(); world->GetAudio().Attach(nullptr);
}

FP_TEST(AudioWorld_StartsPendingLoopsWhenOutputBecomesReady)
{
	SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
	{
		AudioDevice device;
		StrongObjectPtr<World> world(NewObject<World>());
		world->GetAudio().Attach(&device);
		const Entity loop = AddLoop(*world.Get());
		const Entity effect = AddLoop(*world.Get());
		world->Get<AudioSourceComponent>(effect).Loop = false;
		const Entity stopped = AddLoop(*world.Get());
		world->BeginPlay();
		FP_EXPECT(!world->GetAudio().GetSourceVoice(loop));
		world->GetAudio().StopSource(stopped);
		FP_EXPECT(world->GetAudio().PlayOneShot("Audio/Click.wav").Result.Code == AudioError::NotReady);
		FP_EXPECT(device.Initialize());
		world->Update(0.016f);
		const VoiceHandle voice = world->GetAudio().GetSourceVoice(loop);
		FP_EXPECT(bool(voice));
		FP_EXPECT(!world->GetAudio().GetSourceVoice(effect));
		FP_EXPECT(!world->GetAudio().GetSourceVoice(stopped));
		FP_EXPECT(device.GetVoices().Num() == 1);
		world->Update(0.016f);
		FP_EXPECT(world->GetAudio().GetSourceVoice(loop) == voice);
		world->EndPlay();
		world->GetAudio().Attach(nullptr);
	}
	SDL_ResetHint(SDL_HINT_AUDIO_DRIVER);
}

FP_TEST(AudioWorld_OutputRecoveryPreservesPausesStopsAndIsolation)
{
	DummyAudio audio;
	StrongObjectPtr<World> world(NewObject<World>());
	StrongObjectPtr<World> other(NewObject<World>());
	world->GetAudio().Attach(&audio.Device); other->GetAudio().Attach(&audio.Device);
	const Entity loop = AddLoop(*world.Get());
	const Entity paused = AddLoop(*world.Get());
	const Entity stopped = AddLoop(*world.Get());
	const Entity disabled = AddLoop(*world.Get());
	const Entity changed = AddLoop(*world.Get());
	world->Get<AudioSourceComponent>(changed).Loop = false;
	const Entity isolated = AddLoop(*other.Get());
	world->BeginPlay(); other->BeginPlay();
	const VoiceHandle before = world->GetAudio().GetSourceVoice(loop);
	FP_EXPECT(world->GetAudio().Pause(world->GetAudio().GetSourceVoice(paused)));
	FP_EXPECT(world->GetAudio().Stop(world->GetAudio().GetSourceVoice(stopped)));
	world->Get<AudioSourceComponent>(disabled).Enabled = false;
	world->Get<AudioSourceComponent>(changed).Loop = true;
	world->Update(0.016f);
	const auto shot = world->GetAudio().PlayOneShot("Audio/Hum.wav");
	FP_EXPECT(shot);
	world->SetPaused(true);
	// Reopening between scene updates must still be detected.
	audio.Device.Shutdown();
	FP_EXPECT(audio.Device.Initialize());
	world->Step(0.016f); other->Update(0.016f);
	const VoiceHandle recovered = world->GetAudio().GetSourceVoice(loop);
	const VoiceHandle recoveredPaused = world->GetAudio().GetSourceVoice(paused);
	FP_EXPECT(bool(recovered) && recovered != before);
	FP_EXPECT(world->GetAudio().GetState(recovered) == VoiceState::Paused);
	FP_EXPECT(world->GetAudio().GetState(recoveredPaused) == VoiceState::Paused);
	FP_EXPECT(!world->GetAudio().GetSourceVoice(stopped));
	FP_EXPECT(!world->GetAudio().GetSourceVoice(disabled));
	FP_EXPECT(bool(world->GetAudio().GetSourceVoice(changed)));
	FP_EXPECT(world->GetAudio().GetState(shot.Voice) == VoiceState::Invalid);
	FP_EXPECT(bool(other->GetAudio().GetSourceVoice(isolated)));
	world->SetPaused(false);
	FP_EXPECT(world->GetAudio().GetState(recovered) == VoiceState::Playing);
	FP_EXPECT(world->GetAudio().GetState(recoveredPaused) == VoiceState::Paused);
	FP_EXPECT(world->GetAudio().Resume(recoveredPaused));
	// StopAll cancels recovery even while output is unavailable.
	audio.Device.Shutdown(); world->GetAudio().StopAll();
	world->Update(0.016f);
	FP_EXPECT(audio.Device.Initialize());
	world->Update(0.016f); other->Update(0.016f);
	FP_EXPECT(!world->GetAudio().GetSourceVoice(loop));
	FP_EXPECT(bool(other->GetAudio().GetSourceVoice(isolated)));
	world->EndPlay(); other->EndPlay();
	world->GetAudio().Attach(nullptr); other->GetAudio().Attach(nullptr);
}

FP_TEST(AudioWorld_ComponentQueriesPreserveCreationOrder)
{
	DummyAudio audio;
	StrongObjectPtr<World> world(NewObject<World>());
	world->GetAudio().Attach(&audio.Device);
	const Entity recycled = world->CreateEntity("Recycled");
	const Entity first = world->CreateEntity("First listener");
	const Entity second = world->CreateEntity("Second listener");
	world->Add<AudioListenerComponent>(second);
	world->Add<AudioListenerComponent>(first);
	world->DestroyEntity(recycled);
	const Entity late = world->CreateEntity("Late listener");
	world->Add<AudioListenerComponent>(late);
	FP_EXPECT(world->GetCreationIndex(late) > world->GetCreationIndex(first));
	for (int i = 0; i < 2000; ++i) world->CreateEntity("Unrelated");
	std::vector<Entity> sources;
	for (int i = 0; i < 65; ++i) sources.push_back(world->CreateEntity("Source"));
	// Components may be inserted in a different order from their entities.
	for (auto it = sources.rbegin(); it != sources.rend(); ++it)
	{
		auto& source = world->Add<AudioSourceComponent>(*it);
		source.Clip = "Audio/Hum.wav"; source.Loop = source.PlayOnStart = true;
	}
	world->BeginPlay();
	FP_EXPECT(world->GetAudio().GetListenerEntity() == first);
	for (size_t i = 0; i < 64; ++i) FP_EXPECT(bool(world->GetAudio().GetSourceVoice(sources[i])));
	FP_EXPECT(!world->GetAudio().GetSourceVoice(sources.back()));
	world->Get<AudioListenerComponent>(first).Enabled = false;
	world->Update(0.016f);
	FP_EXPECT(world->GetAudio().GetListenerEntity() == second);
	world->Remove<AudioListenerComponent>(second); world->Remove<AudioListenerComponent>(late);
	const Entity camera = world->CreateEntity("Primary camera");
	world->Add<CameraComponent>(camera);
	world->Update(0.016f);
	FP_EXPECT(world->GetAudio().GetListenerEntity() == camera);
	world->EndPlay(); world->GetAudio().Attach(nullptr);
}

FP_TEST(AudioWorld_RepeatedPlayStopAndDeviceLifetime)
{
	DummyAudio audio;
	StrongObjectPtr<World> world(NewObject<World>());
	world->GetAudio().Attach(&audio.Device);
	const Entity source = AddLoop(*world.Get());
	auto* system = world->AddSystem<AudioLifecycleSystem>();
	system->Owner = source;
	VoiceHandle previous;
	for (int i = 0; i < 100; ++i)
	{
		world->BeginPlay();
		FP_EXPECT(world->GetAudio().GetState(previous) == VoiceState::Invalid);
		previous = world->GetAudio().GetSourceVoice(source);
		FP_EXPECT(bool(previous));
		FP_EXPECT(system->HeardStart);
		system->HeardStop = false;
		world->EndPlay();
		FP_EXPECT(system->HeardStop);
		FP_EXPECT(world->GetAudio().GetState(previous) == VoiceState::Invalid);
	}
	world->GetAudio().Attach(nullptr);
	{
		AudioDevice temporary;
		world->GetAudio().Attach(&temporary);
	}
	FP_EXPECT(world->GetAudio().GetDevice() == nullptr);
	world->BeginPlay(); world->EndPlay();
}
