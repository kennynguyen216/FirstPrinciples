#pragma once

#include "DriverSettings.h"
#include "Engine/Math.h"

// The Driver on foot inside the cab. The cab is bigger on the inside: it's a fixed room of its own,
// not a box riding along in the truck, so walking never has to follow the truck through space.

// Where the Driver is standing and looking.
struct DriverState
{
	FP::Vec3 Position; // feet, on the cab floor
	float Yaw = 0;     // degrees
	float Pitch = 0;   // degrees
	bool Seated = false; // in the driver's seat; sitting is never automatic
};

// Walkable area of the cab floor.
struct CabBounds
{
	FP::Vec3 Min;
	FP::Vec3 Max;
};

// Mouse look (degrees this frame) and walking (move axis -1 to 1, Y forward) relative to where the Driver faces.
void StepOnFoot(DriverState& driver, FP::Vec2 look, FP::Vec2 moveAxis, const CabBounds& bounds, const DriverSettings& settings, float deltaTime);

// Close enough to use something in the cab, like the wheel or the scrubber. Ignores height.
bool CanReach(const DriverState& driver, FP::Vec3 point, const DriverSettings& settings);
