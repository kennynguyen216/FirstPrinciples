#pragma once

#include "Engine/Engine.h"

// Real world constants, on the scene's Real Data entity. Each mirrors a value in
// Content/Data/consumption.json, where its source is cited. Nothing in gameplay code repeats them.
// Once a JSON reader exists it can fill this component from that file instead.
struct LifeSupportDataComponent
{
	float OxygenKgPerPersonDay = 0.82f; // NASA BVAD
	float CarbonDioxideKgPerPersonDay = 1.04f; // NASA BVAD
	float WaterKgPerPersonDay = 2.5f; // NASA BVAD, drinking and food preparation
	float IssWaterRecovery = 0.98f; // ISS ECLSS, 2023
	// Oxygen as an ideal gas, for leaks.
	float OxygenGasConstant = 259.8f; // J/(kg K)
	float OxygenHeatCapacityRatio = 1.4f;
};

// Gameplay tuning for a truck's life support, on the Match entity.
struct LifeSupportConfigComponent
{
	FP::int32 CrewAboard = 2;
	// The cab's own supply. The crew breathes this first, then the cargo.
	float CabOxygenReserveKg = 8;
	float CabWaterReserveKg = 10;
	// Share of used water the truck's recycler wins back while it works. The results screen
	// compares it with the ISS.
	float WaterRecovery = 0.85f;
	// How many times the crew's CO2 output a healthy scrubber can remove.
	float ScrubberCapacity = 1.5f;
	// Battery charge per second (of 1) that the scrubber and recycler use, and that a healthy power
	// system makes. Without charge both stop.
	float PowerDrawPerSecond = 0.01f;
	float PowerGenerationPerSecond = 0.015f;
	// Cargo oxygen vented per second of boost.
	float BoostOxygenKgPerSecond = 1.5f;
	// Each leaking tank makes boosting cost this much more, as a fraction.
	float LeakBoostPenalty = 0.25f;
};

// Tank sizes and leak physics, on the Match entity.
struct TankConfigComponent
{
	float OxygenTankKg = 25;
	float WaterTankKg = 40;
	// Leaks are choked flow of an ideal gas through a hole: the flow is proportional to the tank's
	// pressure, which at a fixed volume and temperature is proportional to the oxygen left. So a
	// holed tank loses a fixed fraction per second, faster for bigger holes and smaller tanks.
	float TankVolumeM3 = 0.1f;
	float TankTemperatureK = 293;
	float HoleAreaM2 = 2.5e-5f; // about a 5.6 mm hole
	float DischargeCoefficient = 0.62f; // sharp edged orifice
	// Liquid seeps out at a steady rate per hole.
	float WaterLeakKgPerSecond = 0.4f;
	// Grid spacing in the bay.
	float Spacing = 3;
	FP::int32 PerRow = 6;
};

enum class CargoKind : FP::uint8
{
	Oxygen,
	Water,
};

// One tank in a cargo bay. Spawned by LifeSupportSystem when play starts.
struct CargoTankComponent
{
	FP::int32 Crew = 0;
	CargoKind Kind = CargoKind::Oxygen;
	float AmountKg = 0;
	float CapacityKg = 0;
	// Each hole leaks until patched. Missed shots and Siphons make them.
	FP::int32 Holes = 0;
	// Seconds of repair beam on the current hole.
	float PatchProgress = 0;
};

// One crew's air, water and power. Lives on its own entity, made when play starts, so both
// crew members' systems can find it by crew. Cargo amounts are not stored here: they are the
// sum of the crew's CargoTankComponents.
struct TruckResourcesComponent
{
	FP::int32 Crew = 0;
	float CabOxygenKg = 0;
	float CabWaterKg = 0;
	float CabCarbonDioxideKg = 0; // builds up when the scrubber cannot keep up
	float Power = 1; // battery charge, 0 to 1
	float RadiationDoseMilliSievert = 0; // M6

	// Life support modules, 0 (broken) to 1. Repair stations restore them (M4), hazards damage them (M6).
	float ScrubberHealth = 1;
	float RecyclerHealth = 1;
	float PowerHealth = 1;

	// The crew is breathing cargo oxygen because the cab's ran out.
	bool TappingCargo = false;
	// No oxygen left anywhere. The truck can no longer be flown.
	bool OutOfAir = false;

	// For the results screen.
	float OxygenBreathedKg = 0;
	float OxygenTappedKg = 0;
	float OxygenBoostedKg = 0;
	float OxygenLeakedKg = 0;
	float OxygenEatenKg = 0; // by Siphons
	float OxygenRecoveredKg = 0; // from Siphons that died
	float WaterUsedKg = 0;
	float WaterRecycledKg = 0;
	FP::int32 SiphonsKilled = 0;
	FP::int32 TanksShot = 0; // missed shots that punctured cargo
	FP::int32 HolesPatched = 0;
};

// The colony's oxygen, added to the Colony entity when play starts. Its countdown is how long
// that oxygen lasts its crew at the real consumption rate.
struct ColonyStateComponent
{
	float OxygenKg = 0;
	float OxygenUsePerSecond = 0;
	float WaterReceivedKg = 0;

	float GetSecondsLeft() const { return OxygenUsePerSecond > 0 ? OxygenKg / OxygenUsePerSecond : 0; }
};
