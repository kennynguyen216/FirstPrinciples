#pragma once

#include "Asteroids.h"

#include <vector>

// A mini black hole off the route. Its pull bends the truck in; skimming the ring between the
// core and RingRadius charges a slingshot that fires the hyperdrive boost.
struct BlackHole
{
	FP::Vec3 Position;
	float CoreRadius = 0;
	float RingRadius = 0;
	float Charge = 0;
	bool Spent = false;
};

enum class BlackHoleEvent
{
	None,
	Slingshot,
	Swallowed,
};

std::vector<BlackHole> BuildBlackHoles(const MatchPackage& package, const Route& route, const DriverSettings& settings);

BlackHoleEvent StepBlackHoles(TruckState& truck, std::vector<BlackHole>& holes, const DriverSettings& settings, float deltaTime);
