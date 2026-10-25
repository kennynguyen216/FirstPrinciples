#include "Tests/TestFramework.h"
#include "Engine/Engine.h"
#include "Engine/Physics/PhysicsSystem.h"
#include "Engine/Net/NetworkComponent.h"
#include "Engine/SceneSerializer.h"

#include <cmath>
#include <limits>

using namespace FP;

namespace
{
	Entity Box(World& world, const char* name, Vec3 position, Vec3 size)
	{
		const Entity entity = world.CreateEntity(name);
		world.Get<TransformComponent>(entity).Position = position;
		world.Add<ColliderComponent>(entity).Size = size;
		return entity;
	}
}

FP_TEST(AudioOcclusion_WallsTriggersAndBothEndpointColliders)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	// Overlapping endpoint colliders must not hide a real wall from the query.
	const Entity listener = Box(*world.Get(), "Listener", {}, { 20, 20, 20 });
	world->Add<AudioListenerComponent>(listener);
	const Entity emitter = Box(*world.Get(), "Emitter", { 8, 0, 0 }, { 20, 20, 20 });
	const Entity trigger = Box(*world.Get(), "Trigger", { 2, 0, 0 }, { 0.2f, 4, 4 });
	world->Get<ColliderComponent>(trigger).Trigger = true;
	world->AddSystem<PhysicsSystem>();
	world->BeginPlay();
	OneShotParams params;
	params.Owner = emitter; params.Spatial = true; params.Bus = AudioBus::Voice;
	const auto clear = world->GetAudio().GetSpatialParameters(params);
	FP_EXPECT(clear.LeftGain == 0 && clear.RightGain > 0 && clear.LowPassHz == 0);
	FP_EXPECT(clear.Bus == AudioBus::Voice);
	const Entity wall = Box(*world.Get(), "Wall", { 4, 0, 0 }, { 0.25f, 4, 4 });
	world->Update(0);
	const auto blocked = world->GetAudio().GetSpatialParameters(params);
	FP_EXPECT(std::abs(blocked.RightGain - clear.RightGain * 0.25f) < 0.00001f);
	FP_EXPECT(blocked.LowPassHz == 1200);
	params.Occlusion = false;
	const auto bypass = world->GetAudio().GetSpatialParameters(params);
	FP_EXPECT(bypass.RightGain == clear.RightGain && bypass.LowPassHz == 0);
	params.Occlusion = true;
	world->Get<ColliderComponent>(wall).Trigger = true;
	world->Update(0);
	const auto sensorWall = world->GetAudio().GetSpatialParameters(params);
	FP_EXPECT(sensorWall.RightGain == clear.RightGain && sensorWall.LowPassHz == 0);
	world->Remove<ColliderComponent>(wall);
	world->Update(0);
	const auto removed = world->GetAudio().GetSpatialParameters(params);
	FP_EXPECT(removed.RightGain == clear.RightGain && removed.LowPassHz == 0);
	params.MaxDistance = 2;
	const auto outside = world->GetAudio().GetSpatialParameters(params);
	FP_EXPECT(outside.LeftGain == 0 && outside.RightGain == 0 && outside.LowPassHz == 0);
	world->EndPlay();
}

FP_TEST(AudioOcclusion_ParentedSourceListenerAndWall)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	const Entity listenerParent = world->CreateEntity("Listener parent");
	world->Get<TransformComponent>(listenerParent).Position = { 10, 0, 0 };
	world->Get<TransformComponent>(listenerParent).Rotation = { 0, 180, 0 };
	const Entity listener = Box(*world.Get(), "Listener", {}, { 1, 1, 1 });
	world->Add<AudioListenerComponent>(listener);
	FP_EXPECT(world->SetParent(listener, listenerParent, false));
	const Entity emitterParent = world->CreateEntity("Emitter parent");
	world->Get<TransformComponent>(emitterParent).Position = { 10, 0, 0 };
	const Entity emitter = Box(*world.Get(), "Emitter", { 8, 0, 0 }, { 1, 1, 1 });
	FP_EXPECT(world->SetParent(emitter, emitterParent, false));
	const Entity wallParent = world->CreateEntity("Wall parent");
	world->Get<TransformComponent>(wallParent).Position = { 10, 0, 0 };
	const Entity wall = Box(*world.Get(), "Wall", { 4, 0, 0 }, { 0.25f, 4, 4 });
	FP_EXPECT(world->SetParent(wall, wallParent, false));
	world->AddSystem<PhysicsSystem>();
	world->BeginPlay();
	OneShotParams params;
	params.Owner = emitter; params.Spatial = true;
	const auto blocked = world->GetAudio().GetSpatialParameters(params);
	FP_EXPECT(blocked.LeftGain > 0 && blocked.RightGain < 0.0001f && blocked.LowPassHz == 1200);
	world->Get<TransformComponent>(wallParent).Position.X = 30;
	world->Update(0);
	const auto movedWall = world->GetAudio().GetSpatialParameters(params);
	FP_EXPECT(std::abs(movedWall.LeftGain - blocked.LeftGain * 4) < 0.00001f && movedWall.LowPassHz == 0);
	world->Get<TransformComponent>(emitterParent).Position.X = 0;
	world->Update(0);
	const auto movedSource = world->GetAudio().GetSpatialParameters(params);
	FP_EXPECT(movedSource.LeftGain < 0.0001f && movedSource.RightGain > 0 && movedSource.LowPassHz == 0);
	world->Get<TransformComponent>(emitterParent).Position.X = std::numeric_limits<float>::quiet_NaN();
	const auto invalid = world->GetAudio().GetSpatialParameters(params);
	FP_EXPECT(invalid.LeftGain == 0 && invalid.RightGain == 0);
	world->Get<TransformComponent>(emitterParent).Position.X = 0;
	world->EndPlay();
}

FP_TEST(AudioOcclusion_SettingsRoundTrip)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	const Entity entity = world->CreateEntity("Voice player");
	world->Add<AudioSourceComponent>(entity).Occlusion = false;
	auto& voice = world->Add<VoiceChatComponent>(entity);
	voice.Volume = 0.5f; voice.MinDistance = 2; voice.MaxDistance = 40; voice.Occlusion = false;
	const String saved = SceneSerializer::SaveToString(*world.Get());
	StrongObjectPtr<World> loaded(NewObject<World>());
	FP_EXPECT(SceneSerializer::LoadFromString(*loaded.Get(), saved.View()));
	const Entity player = loaded->FindEntityByName("Voice player");
	FP_EXPECT(!loaded->Get<AudioSourceComponent>(player).Occlusion);
	const auto& back = loaded->Get<VoiceChatComponent>(player);
	FP_EXPECT(back.Enabled && back.Volume == 0.5f && back.MinDistance == 2 && back.MaxDistance == 40 && !back.Occlusion);
	FP_EXPECT(SceneSerializer::SaveToString(*loaded.Get()) == saved);
	FP_EXPECT(AudioSourceComponent{}.Occlusion && OneShotParams{}.Occlusion && VoiceChatComponent{}.Occlusion);
}

FP_TEST(AudioOcclusion_ChildEndpointsIgnoreTheirParentBodies)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	const Entity listenerBody = Box(*world.Get(), "Listener body", {}, { 2, 4, 2 });
	const Entity listener = world->CreateEntity("Listener head");
	world->Get<TransformComponent>(listener).Position = { 0, 0.5f, 0 };
	world->Add<AudioListenerComponent>(listener);
	FP_EXPECT(world->SetParent(listener, listenerBody, false));
	const Entity emitterBody = Box(*world.Get(), "Emitter body", { 8, 0, 0 }, { 2, 4, 2 });
	const Entity neck = world->CreateEntity("Emitter neck");
	FP_EXPECT(world->SetParent(neck, emitterBody, false));
	const Entity emitter = world->CreateEntity("Emitter head");
	world->Get<TransformComponent>(emitter).Position = { 0, 0.5f, 0 };
	FP_EXPECT(world->SetParent(emitter, neck, false));
	world->AddSystem<PhysicsSystem>();
	world->BeginPlay();
	OneShotParams params;
	params.Owner = emitter; params.Spatial = true;
	const auto clear = world->GetAudio().GetSpatialParameters(params);
	FP_EXPECT(clear.LeftGain == 0 && clear.RightGain > 0 && clear.LowPassHz == 0);
	const Entity wall = Box(*world.Get(), "Actual wall", { 4, 0.5f, 0 }, { 0.25f, 4, 4 });
	world->Update(0);
	const auto blocked = world->GetAudio().GetSpatialParameters(params);
	FP_EXPECT(std::abs(blocked.RightGain - clear.RightGain * 0.25f) < 0.00001f && blocked.LowPassHz == 1200);
	world->Remove<ColliderComponent>(wall);
	world->Update(0);
	const auto removed = world->GetAudio().GetSpatialParameters(params);
	FP_EXPECT(removed.RightGain == clear.RightGain && removed.LowPassHz == 0);
	world->EndPlay();
}

FP_TEST(AudioOcclusion_RemoteListenersAndCamerasDoNotCaptureLocalAudio)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	const Entity remoteListener = world->CreateEntity("Remote listener");
	world->Add<AudioListenerComponent>(remoteListener);
	world->Add<NetworkComponent>(remoteListener).Remote = true;
	world->Get<TransformComponent>(remoteListener).Position = { 20, 0, 0 };
	const Entity remoteCamera = world->CreateEntity("Remote camera");
	world->Add<CameraComponent>(remoteCamera);
	world->Add<NetworkComponent>(remoteCamera).Remote = true;
	world->Get<TransformComponent>(remoteCamera).Position = { 20, 0, 0 };
	const Entity localCamera = world->CreateEntity("Local camera");
	world->Add<CameraComponent>(localCamera);
	const Entity localListener = world->CreateEntity("Local listener");
	world->Add<AudioListenerComponent>(localListener);
	world->Add<NetworkComponent>(localListener).Remote = false;
	OneShotParams params;
	params.Position = { 8, 0, 0 }; params.Spatial = true; params.Occlusion = false;
	const auto local = world->GetAudio().GetSpatialParameters(params);
	FP_EXPECT(world->GetAudio().GetListenerEntity() == localListener);
	FP_EXPECT(local.LeftGain == 0 && local.RightGain > 0);
	world->Remove<AudioListenerComponent>(localListener);
	const auto camera = world->GetAudio().GetSpatialParameters(params);
	FP_EXPECT(world->GetAudio().GetListenerEntity() == localCamera);
	FP_EXPECT(camera.LeftGain == 0 && camera.RightGain > 0);
	world->Add<NetworkComponent>(localCamera).Remote = true;
	world->GetAudio().GetSpatialParameters(params);
	FP_EXPECT(world->GetAudio().GetListenerEntity() == NullEntity);
	world->Get<NetworkComponent>(remoteListener).Remote = false;
	const auto newlyLocal = world->GetAudio().GetSpatialParameters(params);
	FP_EXPECT(world->GetAudio().GetListenerEntity() == remoteListener);
	FP_EXPECT(newlyLocal.LeftGain > 0 && newlyLocal.RightGain == 0);
}
