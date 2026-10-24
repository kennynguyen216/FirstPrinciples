#pragma once

#include "LifeSupport/LifeSupportComponents.h"

// Reading and spending a crew's cargo. Every oxygen and water cost in the game goes through
// Draw so that "cargo = breath" holds in one place.
namespace Cargo
{
	struct Totals
	{
		float OxygenKg = 0;
		float OxygenCapacityKg = 0;
		float WaterKg = 0;
		float WaterCapacityKg = 0;
		FP::int32 LeakingTanks = 0;
	};

	Totals Sum(FP::World& world, FP::int32 crew);

	// Takes up to kg from the crew's tanks of one kind and returns how much it got. Empties the
	// lowest open tank first, the way a crew would use one up before opening the next.
	float Draw(FP::World& world, FP::int32 crew, CargoKind kind, float kg);

	// Puts up to kg back into the crew's tanks of one kind and returns how much fit. Fills the
	// emptiest intact tank first, so recovered air does not go straight back out of a hole.
	float Refill(FP::World& world, FP::int32 crew, CargoKind kind, float kg);

	// Fraction of its oxygen a tank loses per second through its holes. Choked flow through an
	// orifice gives mass flow = Cd * A * p * sqrt(k / (R T)) * (2 / (k + 1))^((k + 1) / (2 (k - 1))),
	// and p = m R T / V, so flow / m is a constant for the tank.
	float OxygenLeakRate(const TankConfigComponent& tanks, const LifeSupportDataComponent& data, FP::int32 holes);

	TruckResourcesComponent* FindResources(FP::World& world, FP::int32 crew);

	// The scene's tuning and real data, or defaults when the scene lacks them.
	LifeSupportDataComponent GetData(FP::World& world);
	LifeSupportConfigComponent GetConfig(FP::World& world);
	TankConfigComponent GetTankConfig(FP::World& world);
}
