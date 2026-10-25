#include "Engine/Physics/BlackHoleSystem.h"
#include "Engine/Physics/Physics.h"
#include "Engine/Input.h"

namespace FP
{
	void BlackHoleSystem::OnStart(World& world)
	{
		for (auto [entity, transform, hole] : world.View<TransformComponent, BlackHoleComponent>().each())
			hole.LastPosition = transform.Position;
	}

	void BlackHoleSystem::Update(World& world, float deltaTime)
	{
		if (deltaTime <= 0 || world.View<BlackHoleComponent>().empty())
			return;

		Array<BlackHole::Field> holes;
		for (auto [entity, transform, hole] : world.View<TransformComponent, BlackHoleComponent>().each())
		{
			// Start of the frame plus the hole's own velocity, so Step can move it along during the frame.
			const Vec3 velocity = (transform.Position - hole.LastPosition) / deltaTime;
			holes.Add({ hole.LastPosition, velocity, std::fmax(hole.SchwarzschildRadius, 0.001f), std::fmax(hole.SpeedOfLight, 0.001f) });
			hole.LastPosition = transform.Position;
		}

		const Vec2 axis = Input::GetActionAxis("Move");
		const Vec3 input{ axis.Normalized(), 0 };
		for (auto [entity, transform, body] : world.View<TransformComponent, GravityBodyComponent>().each())
		{
			if (body.Captured)
				continue;
			// Dynamic bodies keep their velocity in physics, which also moves them.
			const RigidBodyComponent* rigidBody = world.TryGet<RigidBodyComponent>(entity);
			const bool dynamic = rigidBody && rigidBody->Motion == "Dynamic";
			Vec3 position = transform.Position;
			Vec3 velocity = dynamic ? world.GetPhysics().GetVelocity(entity) : body.Velocity;
			Array<BlackHole::Field> frame = holes;
			const int32 hit = BlackHole::Step(position, velocity, input * body.Thrust, frame.Data(), frame.Num(), deltaTime);
			if (hit >= 0)
			{
				body.Captured = true;
				velocity = Vec3();
				transform.Position = frame[hit].Position;
			}
			else if (!dynamic)
			{
				transform.Position = position;
			}
			body.Velocity = velocity;
			if (dynamic)
				world.GetPhysics().SetVelocity(entity, velocity);
		}
	}
}
