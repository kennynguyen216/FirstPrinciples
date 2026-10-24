#include "Engine/Scripting/Script.h"

#include <cmath>

using namespace FP;

// First person camera. WASD or the arrows walk relative to where you look, Shift sprints.
// Hold the right mouse button and move the mouse to look around. Space jumps.
// Gravity pulls you down onto the Floor entity's plane while you stand over it; walk off its edge and you fall.
class FirstPersonController : public Script
{
public:
	float MoveSpeed = 4;
	float SprintMultiplier = 2;
	float LookSensitivity = 0.2f; // degrees per pixel
	float Gravity = 20;
	float JumpSpeed = 6;
	float EyeHeight = 1.7f; // camera height above the floor
	String Floor = "Plane"; // name of an entity with a Plane mesh to stand on

	void OnStart() override
	{
		m_LastMouse = Input::GetMousePosition();
	}

	void OnUpdate(float deltaTime) override
	{
		TransformComponent& transform = GetTransform();

		const Vec2 mouse = Input::GetMousePosition();
		if (Input::IsMouseDown(MouseButton::Right))
		{
			const Vec2 delta = mouse - m_LastMouse;
			transform.Rotation.Y -= delta.X * LookSensitivity;
			transform.Rotation.X -= delta.Y * LookSensitivity;
			transform.Rotation.X = transform.Rotation.X > 89 ? 89 : transform.Rotation.X < -89 ? -89 : transform.Rotation.X;
			transform.Rotation.Z = 0;
		}
		m_LastMouse = mouse;

		// Walk on the ground plane, so looking up or down doesn't make you fly.
		const float yaw = transform.Rotation.Y * DegToRad;
		const Vec3 forward{ -std::sin(yaw), 0, -std::cos(yaw) };
		const Vec3 right{ std::cos(yaw), 0, -std::sin(yaw) };
		const Vec2 axis = Input::GetMoveAxis();
		Vec3 move = forward * axis.Y + right * axis.X;
		if (move.Length() > 0)
		{
			const float speed = Input::IsKeyDown(SDL_SCANCODE_LSHIFT) ? MoveSpeed * SprintMultiplier : MoveSpeed;
			transform.Position += move.Normalized() * (speed * deltaTime);
		}

		m_VerticalSpeed -= Gravity * deltaTime;
		transform.Position.Y += m_VerticalSpeed * deltaTime;

		float ground;
		if (GetFloorHeight(transform.Position, ground) && transform.Position.Y <= ground + EyeHeight)
		{
			transform.Position.Y = ground + EyeHeight;
			m_VerticalSpeed = Input::IsKeyDown(SDL_SCANCODE_SPACE) ? JumpSpeed : 0;
		}
	}

private:
	Vec2 m_LastMouse;
	float m_VerticalSpeed = 0;

	// The floor's height under position, when position is inside the plane's square.
	// ponytail: treats the floor as a flat Plane mesh (1x1 scaled by the transform); swap for real colliders once the engine has physics.
	bool GetFloorHeight(Vec3 position, float& height) const
	{
		const Entity floor = FindEntity(Floor.CStr());
		const TransformComponent* floorTransform = GetWorld().IsValid(floor) ? GetWorld().TryGet<TransformComponent>(floor) : nullptr;
		if (!floorTransform)
			return false;
		const Quat rotation = floorTransform->GetRotation();
		const Vec3 local = Quat{ -rotation.X, -rotation.Y, -rotation.Z, rotation.W }.Rotate(position - floorTransform->Position);
		height = floorTransform->Position.Y;
		return std::abs(local.X) <= 0.5f * floorTransform->Scale.X && std::abs(local.Z) <= 0.5f * floorTransform->Scale.Z;
	}
};

FP_SCRIPT(FirstPersonController)
{
	fields.Field("MoveSpeed", &FirstPersonController::MoveSpeed);
	fields.Field("SprintMultiplier", &FirstPersonController::SprintMultiplier);
	fields.Field("LookSensitivity", &FirstPersonController::LookSensitivity);
	fields.Field("Gravity", &FirstPersonController::Gravity);
	fields.Field("JumpSpeed", &FirstPersonController::JumpSpeed);
	fields.Field("EyeHeight", &FirstPersonController::EyeHeight);
	fields.Field("Floor", &FirstPersonController::Floor);
}
