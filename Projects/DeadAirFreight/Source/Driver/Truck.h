#pragma once

#include "DriverSettings.h"
#include "SpaceWeather.h"
#include "Engine/Math.h"

struct TruckState
{
	FP::Vec3 Position;
	FP::Vec3 Destination;
	float Yaw = 0;
	float Pitch = 0;
	// Where the driver looks (yaw, pitch degrees). The nose swings slowly to face it.
	FP::Vec2 Look;
	FP::Vec2 TurnVelocity;
	FP::Vec3 Travel{ 0, 0, -1 };
	float Speed = 0;
	// A rock hit shoves the ship along this, on top of where it travels, dying away.
	FP::Vec3 Knockback;
	float Boost = 0;
	FP::Vec2 JerkRate;
	float NextJerk = 0;
	float NextBoostJerk = 0;
	float BoostJerkSide = 1;
	float Time = 0;
	FP::uint32 Random = 1;
	bool AtWheel = false;
	float WheelEmptyTime = 0;
};

struct TruckControls
{
	FP::Vec2 Aim;
};

struct SpaceWeatherNow
{
	float SolarWind = 0;
	float Kp = 0;
};

FP::Vec3 GetNose(const TruckState& truck);

FP::Vec2 ToYawPitch(FP::Vec3 direction);

// Sends the truck off along a direction. With the slow helm travel follows the nose, so the nose and look turn to it too.
void SetTravel(TruckState& truck, FP::Vec3 direction, const DriverSettings& settings);

FP::Vec3 RotateTowards(FP::Vec3 v, FP::Vec3 target, float maxDegrees);

FP::Vec2 StepDisturbance(TruckState& truck, const SpaceWeatherNow& weather, const DriverSettings& settings, float deltaTime);

float GetDriftAmount(const TruckState& truck, const DriverSettings& settings);

void StepTruck(TruckState& truck, const TruckControls& controls, const SpaceWeatherNow& weather, float disturbanceScale, const DriverSettings& settings, float deltaTime);
