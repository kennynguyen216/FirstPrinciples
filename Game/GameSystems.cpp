#include "Game/GameSystems.h"

#include <cmath>

using namespace FP;

void PlayerSystem::Update(World& world, float deltaTime)
{
	const Vec2 move = Input::GetMoveAxis().Normalized();
	for (auto [entity, transform, player] : world.View<TransformComponent, PlayerComponent>().each())
		transform.Position += move * player.Speed * deltaTime;
}

void SpinnerSystem::Update(World& world, float deltaTime)
{
	for (auto [entity, transform, spinner] : world.View<TransformComponent, SpinnerComponent>().each())
		transform.Rotation = std::fmod(transform.Rotation + spinner.DegreesPerSecond * deltaTime, 360.0f);
}

void CameraFollowSystem::Update(World& world, float deltaTime)
{
	Vec2 target;
	bool found = false;
	for (auto [entity, transform, player] : world.View<TransformComponent, PlayerComponent>().each())
	{
		target = transform.Position;
		found = true;
		break;
	}
	if (!found)
		return;

	for (auto [entity, transform, camera, follow] : world.View<TransformComponent, CameraComponent, CameraFollowComponent>().each())
	{
		const float t = 1.0f - std::exp(-follow.Smoothing * deltaTime);
		transform.Position += (target - transform.Position) * t;
	}
}
