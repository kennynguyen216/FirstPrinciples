#include "Engine/Scripting/Script.h"

#include <cmath>

using namespace FP;

// Floats the entity up and down around where it started.
class Bobber : public Script
{
public:
	FPPROPERTY(Range = "0, 5")
	float Height = 0.5f; // how far it rises and falls, in world units

	FPPROPERTY(Range = "0, 10")
	float Speed = 2; // radians per second

	void OnStart() override
	{
		m_Base = GetTransform().Position;
		m_Time = 0;
	}

	void OnUpdate(float deltaTime) override
	{
		m_Time += deltaTime;
		GetTransform().Position.Y = m_Base.Y + std::sin(m_Time * Speed) * Height;
	}

private:
	Vec3 m_Base;
	float m_Time = 0;
};

FP_SCRIPT(Bobber);
