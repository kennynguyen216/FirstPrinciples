#include "Truck.h"

#include <algorithm>

using namespace FP;

Vec3 GetNose(const TruckState& truck)
{
	return Quat::FromEuler({ truck.Pitch, truck.Yaw, 0 }).Rotate({ 0, 0, -1 });
}

Vec3 RotateTowards(Vec3 v, Vec3 target, float maxDegrees)
{
	const float angle = std::acos(std::clamp(Vec3::Dot(v, target), -1.0f, 1.0f)) * RadToDeg;
	if (angle <= maxDegrees)
		return target;
	Vec3 axis = Vec3::Cross(v, target);
	if (axis.LengthSquared() < 1e-8f)
		axis = { 0, 1, 0 }; // exactly opposite, any perpendicular axis works
	return Quat::AxisAngle(axis, maxDegrees).Rotate(v).Normalized();
}

Vec2 StepDisturbance(TruckState& truck, const SpaceWeatherNow& weather, const DriverSettings& settings, float deltaTime)
{
	// Wobble: a smooth sway, two sines so it never settles into an obvious rhythm.
	const float wind = MapClamped(weather.SolarWind, settings.CalmSolarWind, settings.StormSolarWind, 0, 1);
	const float wobble = settings.CalmWobble + (settings.StormWobble - settings.CalmWobble) * wind;
	const float phase = 2 * Pi * settings.WobbleFrequency * truck.Time;
	const Vec2 sway{ 0.6f * std::sin(phase) + 0.4f * std::sin(1.7f * phase + 1.3f),
		0.6f * std::sin(0.8f * phase + 2.1f) + 0.4f * std::sin(1.9f * phase + 0.4f) };

	// Jerks: sharp kicks that die away, more often the stormier Kp is.
	truck.NextJerk -= deltaTime;
	if (truck.NextJerk <= 0)
	{
		const float storm = MapClamped(weather.Kp, settings.CalmKp, settings.StormKp, 0, 1);
		const float interval = settings.CalmJerkInterval + (settings.StormJerkInterval - settings.CalmJerkInterval) * storm;
		truck.NextJerk = interval * (0.5f + NextRandom(truck.Random)); // +-50% so they can't be timed
		const float angle = NextRandom(truck.Random) * 2 * Pi;
		truck.JerkRate += Vec2{ std::cos(angle), std::sin(angle) } * settings.JerkStrength;
	}
	// Hyperdrive: the truck is too fast to hold. Regular kicks, each swinging roughly the other way.
	if (truck.Boost > 0)
	{
		truck.NextBoostJerk -= deltaTime;
		if (truck.NextBoostJerk <= 0)
		{
			truck.NextBoostJerk += settings.BoostJerkInterval;
			truck.BoostJerkAngle += Pi * (0.6f + 0.8f * NextRandom(truck.Random)); // 108-252 degrees on
			truck.JerkRate += Vec2{ std::cos(truck.BoostJerkAngle), std::sin(truck.BoostJerkAngle) } * settings.BoostJerkStrength;
		}
	}
	else
	{
		truck.NextBoostJerk = 0; // the first kick lands the moment the hyperdrive fires
	}
	truck.JerkRate *= std::exp(-settings.JerkDecay * deltaTime);

	return sway * wobble + truck.JerkRate;
}

float GetDriftAmount(const TruckState& truck, const DriverSettings& settings)
{
	if (truck.AtWheel)
		return 1;
	const float ramp = settings.DriftRampTime - settings.SteadyTime;
	const float drifting = truck.WheelEmptyTime - settings.SteadyTime;
	return drifting <= 0 ? 0 : ramp > 0 ? std::min(drifting / ramp, 1.0f) : 1;
}

void StepTruck(TruckState& truck, const TruckControls& controls, const SpaceWeatherNow& weather, float disturbanceScale, const DriverSettings& settings, float deltaTime)
{
	truck.Time += deltaTime;
	truck.WheelEmptyTime = truck.AtWheel ? 0 : truck.WheelEmptyTime + deltaTime;
	const TruckControls input = truck.AtWheel ? controls : TruckControls{};

	// The mouse aims the nose; space weather pushes it around on top.
	const Vec2 push = StepDisturbance(truck, weather, settings, deltaTime) * (GetDriftAmount(truck, settings) * disturbanceScale);
	truck.Yaw += -input.Aim.X + push.X * deltaTime;
	truck.Pitch += -input.Aim.Y + push.Y * deltaTime;
	truck.Pitch = std::clamp(truck.Pitch, -settings.MaxPitch, settings.MaxPitch);

	truck.Boost = std::max(truck.Boost - deltaTime, 0.0f);
	const float targetSpeed = truck.Boost > 0 ? settings.CruiseSpeed * settings.BoostSpeedScale : settings.CruiseSpeed;
	if (truck.Speed < targetSpeed)
		truck.Speed = std::min(truck.Speed + (truck.Boost > 0 ? settings.BoostAccel : settings.ThrustAccel) * deltaTime, targetSpeed);
	else
		truck.Speed = std::max(truck.Speed - settings.ThrustAccel * deltaTime, targetSpeed);

	// Travel swings toward the nose at a limited rate: point one way and momentum carries you the other.
	// At a standstill there's no momentum, so travel just follows the nose.
	const Vec3 nose = GetNose(truck);
	const float turnRate = settings.TurnTime90 > 0 ? 90 / settings.TurnTime90 : 360;
	truck.Travel = truck.Speed > 0.01f ? RotateTowards(truck.Travel, nose, turnRate * deltaTime) : nose;
	truck.Position += truck.Travel * (truck.Speed * deltaTime);
}
