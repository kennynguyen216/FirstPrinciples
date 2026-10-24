#pragma once

#include "LifeSupport/LifeSupportComponents.h"

// Loads each truck when play starts (its resources and its bay of tanks), then runs the cab's
// life support while racing: the crew breathes and drinks from the cab reserve first and the
// cargo after, the scrubber removes CO2 and the recycler wins back water while there is power.
class LifeSupportSystem : public FP::System
{
public:
	const char* GetName() const override { return "LifeSupport"; }
	void OnStart(FP::World& world) override;
	void Update(FP::World& world, float deltaTime) override;

private:
	void SpawnTanks(FP::World& world, FP::int32 crew);
};

// Leaks holed tanks while racing and shows each tank's state in its material.
class CargoTankSystem : public FP::System
{
public:
	const char* GetName() const override { return "CargoTank"; }
	void Update(FP::World& world, float deltaTime) override;
};

// Burns the colony's oxygen at its crew's real rate while racing. Deliveries add to it.
class ColonyClockSystem : public FP::System
{
public:
	const char* GetName() const override { return "ColonyClock"; }
	void OnStart(FP::World& world) override;
	void Update(FP::World& world, float deltaTime) override;
};

// Writes each crew's meters to the log every few seconds, and when Tab is pressed. Stands in for
// the HUD until the engine can draw text.
class LifeSupportReadoutSystem : public FP::System
{
public:
	const char* GetName() const override { return "LifeSupportReadout"; }
	void Update(FP::World& world, float deltaTime) override;

private:
	float m_SinceLast = 0;
};
