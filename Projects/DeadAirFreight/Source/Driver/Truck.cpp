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
		axis = { 0, 1, 0 };
	return Quat::AxisAngle(axis, maxDegrees).Rotate(v).Normalized();
}

static Vec2 JerkDirection(const TruckState& truck, float offset)
{
	const Vec3 to = (truck.Destination - truck.Position).Normalized();
	const float yawOff = std::remainder(std::atan2(-to.X, -to.Z) * RadToDeg - truck.Yaw, 360.0f);
	const float pitchOff = std::asin(std::clamp(to.Y, -1.0f, 1.0f)) * RadToDeg - truck.Pitch;
	const Vec2 away = Vec2{ -yawOff, -pitchOff }.Normalized();
	const float angle = (away.LengthSquared() > 0 ? std::atan2(away.Y, away.X) : 0) + offset;
	return { std::cos(angle), std::sin(angle) };
}

Vec2 StepDisturbance(TruckState& truck, const SpaceWeatherNow& weather, const DriverSettings& settings, float deltaTime)
{
	const float wind = MapClamped(weather.SolarWind, settings.CalmSolarWind, settings.StormSolarWind, 0, 1);
	const float wobble = settings.CalmWobble + (settings.StormWobble - settings.CalmWobble) * wind;
	const float phase = 2 * Pi * settings.WobbleFrequency * truck.Time;
	const Vec2 sway{ 0.6f * std::sin(phase) + 0.4f * std::sin(1.7f * phase + 1.3f),
		0.6f * std::sin(0.8f * phase + 2.1f) + 0.4f * std::sin(1.9f * phase + 0.4f) };

	truck.NextJerk -= deltaTime;
	if (truck.NextJerk <= 0)
	{
		const float storm = MapClamped(weather.Kp, settings.CalmKp, settings.StormKp, 0, 1);
		const float interval = settings.CalmJerkInterval + (settings.StormJerkInterval - settings.CalmJerkInterval) * storm;
		truck.NextJerk = interval * (0.5f + NextRandom(truck.Random));
		truck.JerkRate += JerkDirection(truck, (NextRandom(truck.Random) * 2 - 1) * 1.2f) * settings.JerkStrength;
	}
	if (truck.Boost > 0)
	{
		truck.NextBoostJerk -= deltaTime;
		if (truck.NextBoostJerk <= 0)
		{
			truck.NextBoostJerk += settings.BoostJerkInterval;
			truck.BoostJerkSide = -truck.BoostJerkSide;
			const float offset = truck.BoostJerkSide * (0.3f + 0.9f * NextRandom(truck.Random));
			truck.JerkRate += JerkDirection(truck, offset) * settings.BoostJerkStrength;
		}
	}
	else
	{
		truck.NextBoostJerk = 0;
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

	const Vec2 push = StepDisturbance(truck, weather, settings, deltaTime) * (GetDriftAmount(truck, settings) * disturbanceScale);
	// Like a ship: the mouse looks around freely and the heading (the blue dot, which the ship faces)
	// swings slowly to meet the look, gathering and shedding speed. Sway and jerks shove the look, never the ship,
	// so the ship only ever drifts by chasing a look that got knocked off. A NoseTurnRate of 0 turns the nose with the mouse instantly.
	Vec2 turn = -input.Aim;
	if (settings.NoseTurnRate > 0)
	{
		truck.Look += turn + push * deltaTime;
		truck.Look.Y = std::clamp(truck.Look.Y, -settings.MaxPitch, settings.MaxPitch);
		const Vec2 lead{ std::remainder(truck.Look.X - truck.Yaw, 360.0f), truck.Look.Y - truck.Pitch };
		auto swing = [&](float offset, float& velocity) {
			const float want = std::clamp(offset * 1.5f, -settings.NoseTurnRate, settings.NoseTurnRate);
			const float accel = settings.NoseTurnAccel * deltaTime;
			velocity += std::clamp(want - velocity, -accel, accel);
			return velocity * deltaTime;
		};
		turn = { swing(lead.X, truck.TurnVelocity.X), swing(lead.Y, truck.TurnVelocity.Y) };
	}
	else
	{
		turn += push * deltaTime;
	}
	truck.Yaw += turn.X;
	truck.Pitch += turn.Y;
	truck.Pitch = std::clamp(truck.Pitch, -settings.MaxPitch, settings.MaxPitch);
	if (settings.NoseTurnRate <= 0)
		truck.Look = { truck.Yaw, truck.Pitch };

	truck.Boost = std::max(truck.Boost - deltaTime, 0.0f);
	const float targetSpeed = truck.Boost > 0 ? settings.CruiseSpeed * settings.BoostSpeedScale : settings.CruiseSpeed;
	if (truck.Speed < targetSpeed)
		truck.Speed = std::min(truck.Speed + (truck.Boost > 0 ? settings.BoostAccel : settings.ThrustAccel) * deltaTime, targetSpeed);
	else
		truck.Speed = std::max(truck.Speed - settings.ThrustAccel * deltaTime, targetSpeed);

	// With the slow helm the ship always travels where it faces. Otherwise travel lags the nose.
	const Vec3 nose = GetNose(truck);
	const float turnRate = settings.TurnTime90 > 0 ? 90 / settings.TurnTime90 : 360;
	truck.Travel = truck.Speed > 0.01f && settings.NoseTurnRate <= 0 ? RotateTowards(truck.Travel, nose, turnRate * deltaTime) : nose;
	truck.Position += truck.Travel * (truck.Speed * deltaTime);
}
