#pragma once

#include "DriverSettings.h"
#include "Engine/Math.h"

struct DriverState
{
	FP::Vec3 Position;
	float Yaw = 0;
	float Pitch = 0;
	bool Seated = false;
	// False while this player is down the hatch in the cargo bay. The cab then leaves the player's
	// input and camera alone (Dead Air Freight's TransitSystem moves them between the two).
	bool InCab = true;
};

struct CabBounds
{
	FP::Vec3 Min;
	FP::Vec3 Max;
	// An open doorway in the back wall: within DoorHalfWidth of DoorX the walker may go past Max.Z
	// up to DoorZ. A negative width means no doorway.
	float DoorX = 0;
	float DoorHalfWidth = -1;
	float DoorZ = 0;
};

void StepOnFoot(DriverState& driver, FP::Vec2 look, FP::Vec2 moveAxis, const CabBounds& bounds, const DriverSettings& settings, float deltaTime);

bool CanReach(const DriverState& driver, FP::Vec3 point, const DriverSettings& settings);
