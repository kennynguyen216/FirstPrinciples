#pragma once

#include "SpaceWeather.h"
#include "Truck.h"

#include <vector>

struct Route
{
	FP::Vec3 Start;
	FP::Vec3 Direction{ 0, 0, -1 };
	float Length = 0;
};

struct Rock
{
	FP::Vec3 Center;
	FP::Vec3 Offset;
	FP::Vec3 Axis;
	float DegreesPerSecond = 0;
	float Radius = 0;
};

std::vector<Rock> BuildRocks(const MatchPackage& package, const Route& route, const DriverSettings& settings);
FP::Vec3 GetRockPosition(const Rock& rock, float matchTime);

bool CollideWithRocks(TruckState& truck, const std::vector<Rock>& rocks, float matchTime, const DriverSettings& settings);
