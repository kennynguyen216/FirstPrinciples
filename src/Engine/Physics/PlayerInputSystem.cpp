#include "Engine/Physics/PlayerInputSystem.h"
#include "Engine/Physics/Physics.h"
#include "Engine/Input.h"

namespace FP
{
	void PlayerInputSystem::Update(World& world, float)
	{
		Physics& physics = world.GetPhysics();
		const Vec2 mouse = Input::GetMouseDelta();
		const Vec2 axis = Input::GetMoveAxis();
		const bool running = Input::IsKeyDown(SDL_SCANCODE_LSHIFT) || Input::IsKeyDown(SDL_SCANCODE_RSHIFT);
		const bool jump = Input::WasKeyPressed(SDL_SCANCODE_SPACE);

		for (auto [entity, player, transform] : world.View<PlayerInputComponent, TransformComponent>().each())
		{
			if (!world.Has<CharacterControllerComponent>(entity))
				continue;

			transform.Rotation.Y -= mouse.X * player.LookSpeed;
			if (TransformComponent* camera = world.TryGet<TransformComponent>(world.FindChild(entity, player.Camera.View())))
				camera->Rotation.X = Clamp(camera->Rotation.X - mouse.Y * player.LookSpeed, -60.0f, 30.0f);

			// Forward and right along the ground, wherever the player faces.
			Vec3 forward = transform.GetForward();
			Vec3 right = transform.GetRight();
			forward.Y = 0;
			right.Y = 0;
			Vec3 direction = forward.Normalized() * axis.Y + right.Normalized() * axis.X;
			if (direction.Length() > 1)
				direction = direction.Normalized();
			physics.MoveCharacter(entity, direction * (running ? player.RunSpeed : player.WalkSpeed));
			if (jump)
				physics.JumpCharacter(entity, player.JumpSpeed);
		}
	}
}
