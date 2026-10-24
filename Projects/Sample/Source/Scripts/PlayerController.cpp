#include "Engine/Scripting/Script.h"

using namespace FP;

// Walks a character controller around with WASD, Shift to run and Space to jump. The mouse turns
// the player and tilts the camera, which is a child of the player so it follows along.
FPCLASS(Category = "Characters")
class PlayerController : public Script
{
public:
	FPPROPERTY(Range = "0, 20")
	float WalkSpeed = 5;

	FPPROPERTY(Range = "0, 30")
	float RunSpeed = 9; // while Shift is held

	FPPROPERTY(Range = "0, 20")
	float JumpSpeed = 6;

	FPPROPERTY(Range = "0.01, 1")
	float LookSpeed = 0.15f; // degrees per pixel of mouse movement

	FPPROPERTY()
	String Camera = "Player Camera"; // the child that tilts up and down

	void OnUpdate(float) override
	{
		const Vec2 mouse = Input::GetMouseDelta();
		GetTransform().Rotation.Y -= mouse.X * LookSpeed;
		if (TransformComponent* camera = TryGet<TransformComponent>(FindChild(Camera.View())))
			camera->Rotation.X = Clamp(camera->Rotation.X - mouse.Y * LookSpeed, -60.0f, 30.0f);

		// Forward and right along the ground, wherever the player faces.
		Vec3 forward = GetTransform().GetForward();
		Vec3 right = GetTransform().GetRight();
		forward.Y = 0;
		right.Y = 0;
		const Vec2 axis = Input::GetMoveAxis();
		Vec3 direction = forward.Normalized() * axis.Y + right.Normalized() * axis.X;
		if (direction.Length() > 1)
			direction = direction.Normalized();
		const bool running = Input::IsKeyDown(SDL_SCANCODE_LSHIFT) || Input::IsKeyDown(SDL_SCANCODE_RSHIFT);
		Move(direction * (running ? RunSpeed : WalkSpeed));

		if (Input::WasKeyPressed(SDL_SCANCODE_SPACE))
			Jump(JumpSpeed);
	}
};

FP_SCRIPT(PlayerController);
