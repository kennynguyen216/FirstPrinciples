#pragma once

#include "Truck/TruckComponents.h"

// Flies the local Driver's truck while racing: W/S thrust, A/D turn, Shift boosts.
class TruckSystem : public FP::System
{
public:
	const char* GetName() const override { return "Truck"; }
	void Update(FP::World& world, float deltaTime) override;
};

// Keeps the camera behind the truck this peer cares about.
class ChaseCameraSystem : public FP::System
{
public:
	const char* GetName() const override { return "ChaseCamera"; }
	void Update(FP::World& world, float deltaTime) override;

private:
	bool m_Snapped = false;
};
