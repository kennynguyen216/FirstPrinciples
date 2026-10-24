#include "Engine/Scripting/Script.h"

#include <cmath>

using namespace FP;

// Walks the entity around with WASD or the arrows and jumps with Space.
// Builds its animation state machine in OnStart: Idle and Walk follow the Speed parameter and
// Jump plays once from any state when the Jump trigger is set.
class NoodleController : public Script
{
public:
	float MoveSpeed = 2.5f;
	float TurnSpeed = 720; // degrees per second

	void OnStart() override
	{
		Animator* animator = GetAnimator();
		if (!animator)
			return;
		animator->AddState("Idle", "Idle");
		animator->AddState("Walk", "Walk");
		animator->AddState("Jump", "Jump", false);
		animator->AddTransition("Idle", "Walk", 0.2f).When("Speed", AnimCompare::Greater, 0.1f);
		animator->AddTransition("Walk", "Idle", 0.25f).When("Speed", AnimCompare::Less, 0.1f);
		animator->AddTransition("", "Jump", 0.1f).When("Jump", AnimCompare::Triggered);
		animator->AddTransition("Jump", "Idle", 0.2f).AfterExitTime(1);
	}

	void OnUpdate(float deltaTime) override
	{
		// W walks away from the camera, which looks down -Z.
		const Vec2 axis = Input::GetMoveAxis();
		Vec3 move{ axis.X, 0, -axis.Y };
		const bool moving = move.Length() > 0;
		if (moving)
			move = move.Normalized();

		TransformComponent& transform = GetTransform();
		transform.Position += move * (MoveSpeed * deltaTime);
		if (moving)
		{
			// The mesh faces +Z, so yaw 0 looks at the camera.
			const float target = std::atan2(move.X, move.Z) * RadToDeg;
			float turn = std::fmod(target - transform.Rotation.Y + 540.0f, 360.0f) - 180.0f;
			const float step = TurnSpeed * deltaTime;
			turn = turn > step ? step : turn < -step ? -step : turn;
			transform.Rotation.Y += turn;
		}

		if (Animator* animator = GetAnimator())
		{
			animator->SetFloat("Speed", moving ? MoveSpeed : 0);
			if (Input::WasKeyPressed(SDL_SCANCODE_SPACE))
				animator->SetTrigger("Jump");
		}
	}
};

FP_SCRIPT(NoodleController)
{
	fields.Field("MoveSpeed", &NoodleController::MoveSpeed);
	fields.Field("TurnSpeed", &NoodleController::TurnSpeed);
}
