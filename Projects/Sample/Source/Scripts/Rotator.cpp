#include "Engine/Scripting/Script.h"

using namespace FP;

// Turns the entity every frame.
class Rotator : public Script
{
public:
	FPPROPERTY()
	Vec3 DegreesPerSecond{ 0, 90, 0 }; // spin around each axis

	// Spins the other way.
	FPFUNCTION()
	void Reverse() { DegreesPerSecond = DegreesPerSecond * -1.0f; }

	void OnUpdate(float deltaTime) override
	{
		GetTransform().Rotation += DegreesPerSecond * deltaTime;
	}
};

FP_SCRIPT(Rotator);
