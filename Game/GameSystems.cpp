#include "Game/GameSystems.h"

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
		transform.Rotation.Z = std::fmod(transform.Rotation.Z + spinner.DegreesPerSecond * deltaTime, 360.0f);
}

void CameraFollowSystem::Update(World& world, float deltaTime)
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

	for (auto [entity, transform, camera, follow] : world.View<TransformComponent, CameraComponent, CameraFollowComponent>().each())
	{
		// Follow in X and Y only so the camera keeps its own depth.
		const float t = 1.0f - std::exp(-follow.Smoothing * deltaTime);
		const Vec3 goal{ target.X, target.Y, transform.Position.Z };
		transform.Position += (goal - transform.Position) * t;
	}
}
