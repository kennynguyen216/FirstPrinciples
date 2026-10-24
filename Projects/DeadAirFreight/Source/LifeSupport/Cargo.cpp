#include "LifeSupport/Cargo.h"

#include <algorithm>
#include <cmath>

using namespace FP;

namespace
{
	template <typename T>
	T FindOrDefault(World& world)
	{
		for (auto [entity, found] : world.View<T>().each())
			return found;
		return T{};
	}
}

namespace Cargo
{
	Totals Sum(World& world, int32 crew)
	{
		Totals totals;
		for (auto [entity, tank] : world.View<CargoTankComponent>().each())
		{
			if (tank.Crew != crew)
				continue;
			if (tank.Kind == CargoKind::Oxygen)
			{
				totals.OxygenKg += tank.AmountKg;
				totals.OxygenCapacityKg += tank.CapacityKg;
			}
			else
			{
				totals.WaterKg += tank.AmountKg;
				totals.WaterCapacityKg += tank.CapacityKg;
			}
			if (tank.Holes > 0 && tank.AmountKg > 0)
				++totals.LeakingTanks;
		}
		return totals;
	}

	float Draw(World& world, int32 crew, CargoKind kind, float kg)
	{
		float drawn = 0;
		while (drawn < kg)
		{
			CargoTankComponent* lowest = nullptr;
			for (auto [entity, tank] : world.View<CargoTankComponent>().each())
			{
				if (tank.Crew == crew && tank.Kind == kind && tank.AmountKg > 0 && (!lowest || tank.AmountKg < lowest->AmountKg))
					lowest = &tank;
			}
			if (!lowest)
				break;
			const float take = std::min(lowest->AmountKg, kg - drawn);
			lowest->AmountKg -= take;
			drawn += take;
		}
		return drawn;
	}

	float Refill(World& world, int32 crew, CargoKind kind, float kg)
	{
		float stored = 0;
		while (stored < kg)
		{
			CargoTankComponent* emptiest = nullptr;
			for (auto [entity, tank] : world.View<CargoTankComponent>().each())
			{
				if (tank.Crew != crew || tank.Kind != kind || tank.AmountKg >= tank.CapacityKg)
					continue;
				// Intact tanks first, then the emptiest.
				if (!emptiest || (tank.Holes == 0) > (emptiest->Holes == 0) ||
					((tank.Holes == 0) == (emptiest->Holes == 0) && tank.AmountKg < emptiest->AmountKg))
					emptiest = &tank;
			}
			if (!emptiest)
				break;
			const float put = std::min(emptiest->CapacityKg - emptiest->AmountKg, kg - stored);
			emptiest->AmountKg += put;
			stored += put;
		}
		return stored;
	}

	float OxygenLeakRate(const TankConfigComponent& tanks, const LifeSupportDataComponent& data, int32 holes)
	{
		if (holes <= 0 || tanks.TankVolumeM3 <= 0)
			return 0;
		const float k = data.OxygenHeatCapacityRatio;
		const float choke = std::pow(2.0f / (k + 1.0f), (k + 1.0f) / (2.0f * (k - 1.0f)));
		const float soundSpeed = std::sqrt(k * data.OxygenGasConstant * tanks.TankTemperatureK);
		return holes * tanks.DischargeCoefficient * tanks.HoleAreaM2 * soundSpeed * choke / tanks.TankVolumeM3;
	}

	TruckResourcesComponent* FindResources(World& world, int32 crew)
	{
		for (auto [entity, resources] : world.View<TruckResourcesComponent>().each())
		{
			if (resources.Crew == crew)
				return &resources;
		}
		return nullptr;
	}

	LifeSupportDataComponent GetData(World& world) { return FindOrDefault<LifeSupportDataComponent>(world); }
	LifeSupportConfigComponent GetConfig(World& world) { return FindOrDefault<LifeSupportConfigComponent>(world); }
	TankConfigComponent GetTankConfig(World& world) { return FindOrDefault<TankConfigComponent>(world); }
}
