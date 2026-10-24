#pragma once

#include "SpaceWeather.h"
#include "Truck.h"

#include <vector>

// Asteroid fields from the Match Package. Each rock circles its cluster's center, and its position
// is a function of match time only, so every machine sees the same rocks without syncing them.

// The straight line from depot to colony.
struct Route
{
	FP::Vec3 Start;
	FP::Vec3 Direction{ 0, 0, -1 }; // unit
	float Length = 0;
};

struct Rock
{
	FP::Vec3 Center; // cluster center
	FP::Vec3 Offset; // from the center at time 0
	FP::Vec3 Axis;   // it circles around
	float DegreesPerSecond = 0;
	float Radius = 0;
};

std::vector<Rock> BuildRocks(const MatchPackage& package, const Route& route, const DriverSettings& settings);
FP::Vec3 GetRockPosition(const Rock& rock, float matchTime);

// Bounces the truck off the first rock it overlaps: pushed out, travel reflected, most speed lost,
// and a strong jerk to the aim. True if it hit.
bool CollideWithRocks(TruckState& truck, const std::vector<Rock>& rocks, float matchTime, const DriverSettings& settings);
