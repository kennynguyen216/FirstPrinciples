#include "Cab.h"

#include <algorithm>

using namespace FP;

void StepOnFoot(DriverState& driver, Vec2 look, Vec2 moveAxis, const CabBounds& bounds, const DriverSettings& settings, float deltaTime)
{
	driver.Yaw -= look.X;
	driver.Pitch = std::clamp(driver.Pitch - look.Y, -89.0f, 89.0f);

	const float yaw = driver.Yaw * DegToRad;
	const Vec3 forward{ -std::sin(yaw), 0, -std::cos(yaw) };
	const Vec3 right{ std::cos(yaw), 0, -std::sin(yaw) };
	const Vec3 move = forward * moveAxis.Y + right * moveAxis.X;
	if (move.Length() > 0)
		driver.Position += move.Normalized() * (settings.WalkSpeed * deltaTime);

	driver.Position.X = std::clamp(driver.Position.X, bounds.Min.X, bounds.Max.X);
	driver.Position.Z = std::clamp(driver.Position.Z, bounds.Min.Z, bounds.Max.Z);
}

bool CanReach(const DriverState& driver, Vec3 point, const DriverSettings& settings)
{
	const Vec3 offset = point - driver.Position;
	return Vec3{ offset.X, 0, offset.Z }.Length() <= settings.InteractRange;
}
