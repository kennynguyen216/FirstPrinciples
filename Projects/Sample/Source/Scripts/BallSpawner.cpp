#include "Engine/Scripting/Script.h"

using namespace FP;

// Drops a bouncy ball from above the scene every so often, at a random spot.
// Space kicks every ball upward. Works best as a scene script.
FPCLASS(Category = "Physics")
class BallSpawner : public Script
{
public:
	FPPROPERTY(Range = "0.1, 5")
	float Interval = 0.8f; // seconds between balls

	FPPROPERTY(Range = "1, 200")
	int32 MaxBalls = 40; // the oldest ball goes once there are this many

	FPPROPERTY()
	Vec3 DropFrom{ -6.5f, 8, 0 };

	FPPROPERTY()
	String Material = "Materials/Gold.material";

	void OnStart() override
	{
		Every(Interval, [this]() { Drop(); });
	}

	void OnUpdate(float) override
	{
		if (Input::WasKeyPressed(SDL_SCANCODE_SPACE))
			KickAll();
	}

	FPFUNCTION(Tooltip = "Drops one ball now")
	void Drop()
	{
		const Vec3 jitter{ Random::Range(-0.5f, 0.5f), 0, Random::Range(-1.0f, 1.0f) };
		const Entity ball = Spawn("Ball", DropFrom + jitter);
		GetWorld().Get<TransformComponent>(ball).Scale = { 0.5f, 0.5f, 0.5f };
		GetWorld().Add<MeshComponent>(ball).Mesh = "Sphere";
		GetWorld().Add<MaterialComponent>(ball).Material = Material;
		GetWorld().Add<ColliderComponent>(ball).Shape = "Sphere";
		RigidBodyComponent& body = GetWorld().Add<RigidBodyComponent>(ball);
		body.Restitution = 0.6f;
		body.Mass = 0.5f;
		m_Balls.Add(ball);
		if (m_Balls.Num() > MaxBalls)
		{
			Destroy(m_Balls[0]);
			m_Balls.RemoveAt(0);
		}
	}

	FPFUNCTION(Tooltip = "Kicks every ball upward, like pressing Space")
	void KickAll()
	{
		for (Entity ball : m_Balls)
		{
			if (IsValid(ball))
				GetPhysics().AddImpulse(ball, { Random::Range(-1.0f, 1.0f), 4, Random::Range(-1.0f, 1.0f) });
		}
	}

private:
	Array<Entity> m_Balls;
};

FP_SCRIPT(BallSpawner);
