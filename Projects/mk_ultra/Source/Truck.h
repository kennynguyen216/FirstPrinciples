#pragma once

#include "DriverSettings.h"
#include "SpaceWeather.h"
#include "Engine/Math.h"

// The truck's flight, kept apart from input and display. Everything that changes during a run is in
// TruckState, so networking can sync this one struct, and StepTruck is a plain function of its inputs.

// Everything about the truck that changes while flying.
struct TruckState
{
	FP::Vec3 Position;
	float Yaw = 0;   // nose, degrees
	float Pitch = 0; // nose, degrees
	FP::Vec3 Travel{ 0, 0, -1 }; // unit direction the truck is actually moving, lags behind the nose
	float Speed = 0;
	float Boost = 0; // seconds of jackpot hyperdrive left; speed runs at CruiseSpeed * BoostSpeedScale
	FP::Vec2 JerkRate; // deg/s yaw and pitch kick from the last jerk, dying away
	float NextJerk = 0; // seconds until the next jerk
	float NextBoostJerk = 0; // seconds until the next hyperdrive jerk
	float BoostJerkAngle = 0; // radians, direction of the last hyperdrive jerk; each one swings roughly opposite
	float Time = 0; // seconds into the match; the match clock
	FP::uint32 Random = 1; // seed; same seed and inputs give the same flight on every machine
	bool AtWheel = false; // someone is in the driver's seat; sitting is never automatic
	float WheelEmptyTime = 0; // seconds since the wheel was left, for steady-then-drift
};

// What the Driver is asking for this frame.
struct TruckControls
{
	FP::Vec2 Aim; // degrees to turn the nose this frame; X turns right, Y pitches down, like the mouse
};

// Space weather right now. M5 fills this from the Match Package timeline.
struct SpaceWeatherNow
{
	float SolarWind = 0; // km/s
	float Kp = 0;        // 0-9
};

// Direction the nose points.
FP::Vec3 GetNose(const TruckState& truck);

// v turned toward target by at most maxDegrees. Both unit length.
FP::Vec3 RotateTowards(FP::Vec3 v, FP::Vec3 target, float maxDegrees);

// Wobble from solar wind plus jerks from Kp, as deg/s on yaw and pitch. Advances the jerk timers.
// During the hyperdrive, steady jerks every BoostJerkInterval swing back and forth on top.
FP::Vec2 StepDisturbance(TruckState& truck, const SpaceWeatherNow& weather, const DriverSettings& settings, float deltaTime);

// How much of the disturbance gets through while nobody is steering: 0 during the steady phase,
// ramping to 1 by DriftRampTime. Always 1 at the wheel.
float GetDriftAmount(const TruckState& truck, const DriverSettings& settings);

// One frame of flight: aim, disturbance, sliding travel direction. The drive is always on: speed
// climbs to CruiseSpeed and back there after a hit, whoever is at the wheel. While Boost lasts it
// punches up to CruiseSpeed * BoostSpeedScale at BoostAccel, then eases back down at ThrustAccel.
// Controls only count while AtWheel. disturbanceScale multiplies wobble and jerks (CO2, Calm Hands).
void StepTruck(TruckState& truck, const TruckControls& controls, const SpaceWeatherNow& weather, float disturbanceScale, const DriverSettings& settings, float deltaTime);
