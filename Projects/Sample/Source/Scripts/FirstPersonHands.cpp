#include "Engine/Scripting/Script.h"

using namespace FP;

// A flying first person camera that pokes at the physics scene. C switches between it and the
// scene's main camera. While looking through it: the mouse looks, WASD flies, left click
// pushes what is in the middle of the screen, F picks it up or drops it, right click blows up
// with a burst of sparks.
FPCLASS(Category = "Physics")
class FirstPersonHands : public Script
{
public:
	FPPROPERTY()
	String OtherCamera = "Main Camera";

	FPPROPERTY(Range = "0.01, 1")
	float LookSpeed = 0.15f; // degrees per pixel of mouse movement

	FPPROPERTY(Range = "0, 20")
	float MoveSpeed = 6;

	FPPROPERTY(Range = "1, 100")
	float Reach = 30;

	FPPROPERTY(Range = "0, 50")
	float Push = 6; // newton seconds per click

	FPPROPERTY(Range = "0.5, 10")
	float HoldDistance = 3;

	FPPROPERTY(Range = "0.5, 10")
	float BlastRadius = 4;

	FPPROPERTY(Range = "0, 100")
	float BlastImpulse = 15;

	void OnUpdate(float deltaTime) override
	{
		if (Input::WasKeyPressed(SDL_SCANCODE_C))
		{
			Drop();
			SetActiveCamera(IsActiveCamera() ? FindEntity(OtherCamera.View()) : GetEntity());
		}
		if (!IsActiveCamera())
			return;

		Look();
		Fly(deltaTime);

		RaycastHit hit;
		const bool aimed = RaycastFromCamera(Reach, hit);
		if (Input::WasMousePressed(MouseButton::Left) && aimed)
			GetPhysics().AddImpulseAtPoint(hit.Entity, GetTransform().GetForward() * Push, hit.Point);
		if (Input::WasMousePressed(MouseButton::Right) && aimed)
		{
			AddExplosion(hit.Point, BlastRadius, BlastImpulse);
			Sparks(hit.Point);
		}
		if (Input::WasKeyPressed(SDL_SCANCODE_F))
		{
			if (m_Held != NullEntity)
				Drop();
			else if (aimed && GetPhysics().GetMass(hit.Entity) > 0)
				m_Held = hit.Entity;
		}
		Hold();
	}

	void OnStop() override { Drop(); }

private:
	Entity m_Held = NullEntity;

	void Look()
	{
		const Vec2 mouse = Input::GetMouseDelta();
		Vec3& rotation = GetTransform().Rotation;
		rotation.Y -= mouse.X * LookSpeed;
		rotation.X = Clamp(rotation.X - mouse.Y * LookSpeed, -89.0f, 89.0f);
	}

	void Fly(float deltaTime)
	{
		const Vec2 axis = Input::GetMoveAxis();
		TransformComponent& transform = GetTransform();
		transform.Position += (transform.GetForward() * axis.Y + transform.GetRight() * axis.X) * (MoveSpeed * deltaTime);
	}

	// Pulls the held body toward a point in front of the camera, so it still bumps into things.
	void Hold()
	{
		if (m_Held == NullEntity)
			return;
		if (!IsValid(m_Held))
		{
			m_Held = NullEntity;
			return;
		}
		const TransformComponent view = GetWorld().GetWorldTransform(GetEntity());
		const Vec3 target = view.Position + view.GetForward() * HoldDistance;
		const Vec3 offset = target - GetWorld().GetWorldPosition(m_Held);
		if (offset.Length() > HoldDistance * 3)
		{
			Drop();
			return;
		}
		GetPhysics().SetVelocity(m_Held, offset * 12.0f);
		GetPhysics().SetAngularVelocity(m_Held, GetPhysics().GetAngularVelocity(m_Held) * 0.8f);
	}

	void Drop() { m_Held = NullEntity; }

	// A one off burst of sparks that cleans itself up.
	void Sparks(Vec3 at)
	{
		const Entity blast = Spawn("Sparks", at);
		ParticleEmitterComponent& sparks = GetWorld().Add<ParticleEmitterComponent>(blast);
		sparks.Rate = 0;
		sparks.Burst = 80;
		sparks.Spread = 180;
		sparks.Speed = 6;
		sparks.Gravity = 1;
		sparks.Lifetime = 0.8f;
		sparks.StartSize = 0.12f;
		sparks.EndSize = 0.02f;
		sparks.DestroyWhenDone = true;
	}
};

FP_SCRIPT(FirstPersonHands);
