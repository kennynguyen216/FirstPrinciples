#include "GameSystems.h"
#include "Engine/Net/NetworkComponent.h"
#include "Engine/Physics/Physics.h"

#include <cmath>
#include <limits>

using namespace FP;

namespace
{
	bool IsRemote(World& world, Entity entity)
	{
		const NetworkComponent* network = world.TryGet<NetworkComponent>(entity);
		return network && network->Remote;
	}
}

void PlayerSystem::Update(World& world, float deltaTime)
{
	const Vec3 move{ Input::GetMoveAxis().Normalized(), 0 };
	for (auto [entity, transform, player] : world.View<TransformComponent, PlayerComponent>().each())
	{
		// Other peers' players move on their own machines.
		if (IsRemote(world, entity))
			continue;
		transform.Position += move * player.Speed * deltaTime;
	}
}

void SpinnerSystem::Update(World& world, float deltaTime)
{
	for (auto [entity, transform, spinner] : world.View<TransformComponent, SpinnerComponent>().each())
	{
		transform.Rotation += spinner.Axis * (spinner.DegreesPerSecond * deltaTime);
		transform.Rotation = { std::fmod(transform.Rotation.X, 360.0f), std::fmod(transform.Rotation.Y, 360.0f), std::fmod(transform.Rotation.Z, 360.0f) };
	}
}

void FollowPlayerSystem::Update(World& world, float deltaTime)
{
	Vec3 target;
	bool found = false;
	for (auto [entity, transform, player] : world.View<TransformComponent, PlayerComponent>().each())
	{
		// In multiplayer the camera follows this peer's own player.
		if (IsRemote(world, entity))
			continue;
		target = transform.Position;
		found = true;
		break;
	}
	if (!found)
		return;

	for (auto [entity, transform, follow] : world.View<TransformComponent, FollowPlayerComponent>().each())
	{
		// Follow in X and Y only so the follower keeps its own depth.
		const float t = 1.0f - std::exp(-follow.Smoothing * deltaTime);
		const Vec3 goal{ target.X, target.Y, transform.Position.Z };
		transform.Position += (goal - transform.Position) * t;
	}
}

void AudioDemoSystem::OnStart(World& world)
{
	for (auto [entity, demo] : world.View<AudioDemoComponent>().each())
	{
		const AudioResult loaded = world.GetAudio().Preload(demo.Clip);
		if (!loaded) FP_LOG_WARN("Audio demo: %s", loaded.Message.CStr());
	}
}

void AudioDemoSystem::Update(World& world, float)
{
	if (!Input::WasKeyPressed(SDL_SCANCODE_SPACE))
		return;
	for (auto [entity, demo] : world.View<AudioDemoComponent>().each())
	{
		const AudioPlayResult played = world.GetAudio().PlayOneShot(demo.Clip);
		if (!played) FP_LOG_WARN("Audio demo: %s", played.Result.Message.CStr());
	}
}

void AcousticsTestSystem::OnStart(World& world)
{
	for (auto [entity, transform, moving] : world.View<TransformComponent, MovingAudioComponent>().each())
	{
		moving.Origin = transform.Position; moving.Phase = 0;
	}
	if (world.View<AcousticsTestComponent>().empty()) return;
	const auto loaded = world.GetAudio().Preload("Audio/Clap.wav");
	if (!loaded) FP_LOG_WARN("Acoustics test: %s", loaded.Message.CStr());
	const auto tone = world.GetAudio().Preload("Audio/DopplerTone.wav");
	if (!tone) FP_LOG_WARN("Doppler demo: %s", tone.Message.CStr());
	FP_LOG("3D acoustics: WASD walk, mouse look, 1 soft room, 2 bright room, 3 hall, 4 outdoors, 5 moving Doppler tone. P toggles Doppler. Space clap, B overlap test, T auto-clap, R room effects on/off.");
}

void AcousticsTestSystem::Update(World& world, float deltaTime)
{
	for (auto [entity, transform, test] : world.View<TransformComponent, AcousticsTestComponent>().each())
	{
		if (IsRemote(world, entity)) continue;
		const float centers[] = {-18, 0, 24, 48};
		for (int i = 0; i < 4; ++i)
			if (Input::WasKeyPressed(Key(SDL_SCANCODE_1 + i)))
			{
				test.DopplerDemo = false;
				for (auto [movingEntity, moving, source] : world.View<MovingAudioComponent, AudioSourceComponent>().each())
				{
					source.Enabled = false; world.GetAudio().StopSource(movingEntity);
				}
				transform.Position = {centers[i], 1.65f, i == 2 ? 8.f : 3.f};
				transform.Rotation = {};
				test.UntilClap = 0.4f;
			}
		if (Input::WasKeyPressed(SDL_SCANCODE_5))
		{
			test.DopplerDemo = true;
			transform.Position = {24, 1.65f, 4}; transform.Rotation = {};
			for (auto [movingEntity, moving, source] : world.View<MovingAudioComponent, AudioSourceComponent>().each())
			{
				source.Enabled = true;
				const auto played = world.GetAudio().Play(movingEntity);
				if (!played) FP_LOG_WARN("Doppler demo: %s", played.Result.Message.CStr());
			}
		}
		if (Input::WasKeyPressed(SDL_SCANCODE_P))
			for (auto [movingEntity, moving, source] : world.View<MovingAudioComponent, AudioSourceComponent>().each())
				source.DopplerScale = source.DopplerScale > 0 ? 0.f : 1.f;
		const Vec2 mouse = Input::GetMouseDelta();
		transform.Rotation.Y -= mouse.X * 0.12f;
		transform.Rotation.X = Clamp(transform.Rotation.X - mouse.Y * 0.12f, -80.f, 80.f);
		const Vec2 axis = Input::GetMoveAxis();
		Vec3 forward = transform.GetForward(); forward.Y = 0; forward = forward.Normalized();
		Vec3 right = transform.GetRight(); right.Y = 0; right = right.Normalized();
		const Vec3 move = (forward * axis.Y + right * axis.X).Normalized() * (5.f * std::min(deltaTime, 0.05f));
		// Slide along walls, including door jambs, without needing a dynamic player body.
		for (Vec3 step : {Vec3{move.X, 0, 0}, Vec3{0, 0, move.Z}})
		{
			RaycastHit hit;
			if (step.Length() > 0 && !world.GetPhysics().SphereCast(transform.Position, 0.25f, step, step.Length(), hit))
				transform.Position += step;
		}
		if (Input::WasKeyPressed(SDL_SCANCODE_R))
		{
			test.Bypass = !test.Bypass;
			for (auto [roomEntity, room] : world.View<AudioRoomComponent>().each()) room.Enabled = !test.Bypass;
			FP_LOG("Room reflections and reverb: %s", test.Bypass ? "OFF (dry comparison)" : "ON");
			test.UntilClap = 0.2f;
		}
		if (Input::WasKeyPressed(SDL_SCANCODE_T)) test.AutoClap = !test.AutoClap;
		test.UntilClap -= deltaTime;
		const bool burst = Input::WasKeyPressed(SDL_SCANCODE_B);
		if (Input::WasKeyPressed(SDL_SCANCODE_SPACE) || burst || (!test.DopplerDemo && test.AutoClap && test.UntilClap <= 0))
		{
			Entity emitter = NullEntity;
			float nearest = std::numeric_limits<float>::max();
			for (auto [candidate, source] : world.View<AudioSourceComponent>().each())
			{
				if (!source.Enabled || !source.Spatial || source.Loop || !world.Has<TransformComponent>(candidate)) continue;
				const float distance = (world.GetWorldPosition(candidate) - transform.Position).LengthSquared();
				if (distance < nearest) { nearest = distance; emitter = candidate; }
			}
			test.UntilClap = 4;
			if (emitter == NullEntity) continue;
			const auto& source = world.Get<AudioSourceComponent>(emitter);
			OneShotParams params;
			params.Spatial = true; params.Owner = emitter;
			params.Volume = source.Volume; params.Pitch = source.Pitch;
			params.MinDistance = source.MinDistance; params.MaxDistance = source.MaxDistance;
			params.Occlusion = source.Occlusion; params.Priority = source.Priority;
			params.DopplerScale = source.DopplerScale;
			if (source.Bus == "Music") params.Bus = AudioBus::Music;
			else if (source.Bus == "UI") params.Bus = AudioBus::UI;
			else if (source.Bus == "Voice") params.Bus = AudioBus::Voice;
			for (int i = 0; i < (burst ? 16 : 1); ++i)
			{
				const auto played = world.GetAudio().PlayOneShot(source.Clip, params);
				if (!played) FP_LOG_WARN("Acoustics test: %s", played.Result.Message.CStr());
			}
		}
	}
	for (auto [entity, transform, moving, source] : world.View<TransformComponent, MovingAudioComponent, AudioSourceComponent>().each())
	{
		if (!source.Enabled) { moving.Phase = 0; transform.Position = moving.Origin; continue; }
		if (!std::isfinite(moving.Distance) || moving.Distance < 0.1f || moving.Distance > 100
			|| !std::isfinite(moving.Speed) || moving.Speed < 0 || moving.Speed > 150) continue;
		moving.Phase = std::fmod(moving.Phase + moving.Speed / moving.Distance * std::clamp(deltaTime, 0.f, 0.05f), 2 * Pi);
		transform.Position = moving.Origin + Vec3{std::sin(moving.Phase) * moving.Distance, 0, 0};
	}
}
