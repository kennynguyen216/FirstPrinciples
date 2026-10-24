#include "GameSystems.h"

#include <cmath>

using namespace FP;

void PlayerSystem::Update(World& world, float deltaTime)
{
	const Vec3 move{ Input::GetMoveAxis().Normalized(), 0 };
	for (auto [entity, transform, player] : world.View<TransformComponent, PlayerComponent>().each())
		transform.Position += move * player.Speed * deltaTime;
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
