#include "Engine/Scripting/Script.h"

using namespace FP;

// Turns the entity every frame.
class Rotator : public Script
{
public:
	Vec3 DegreesPerSecond{ 0, 90, 0 };

	void OnUpdate(float deltaTime) override
	{
		GetTransform().Rotation += DegreesPerSecond * deltaTime;
	}
};

FP_SCRIPT(Rotator)
{
	fields.Field("DegreesPerSecond", &Rotator::DegreesPerSecond);
}
