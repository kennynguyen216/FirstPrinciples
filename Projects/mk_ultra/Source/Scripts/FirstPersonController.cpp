#include "Engine/Scripting/Script.h"

#include <cmath>

using namespace FP;

class FirstPersonController : public Script
{
public:
	float MoveSpeed = 4;
	float SprintMultiplier = 2;
	float LookSensitivity = 0.2f;
	float Gravity = 20;
	float JumpSpeed = 6;
	float EyeHeight = 1.7f;
	String Floor = "Plane";

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
