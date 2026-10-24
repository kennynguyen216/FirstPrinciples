#include "LifeSupport/LifeSupportSystems.h"
#include "LifeSupport/Cargo.h"
#include "Enemies/SiphonSystems.h"
#include "Match/MatchQueries.h"
#include "Physics/PhysicsComponents.h"

#include <algorithm>
#include <cmath>

using namespace FP;
using namespace MatchQueries;

namespace
{
	// Tanks stand this tall and wide before the real models arrive.
	const Vec3 TankScale{ 1.2f, 2.4f, 1.2f };

	bool IsRacing(World& world)
	{
		const MatchStateComponent* state = GetState(world);
		return state && state->Phase == MatchPhase::Racing;
	}

	MatchConfigComponent GetMatchConfig(World& world)
	{
		const Entity match = FindMatch(world);
		return match == NullEntity ? MatchConfigComponent{} : world.Get<MatchConfigComponent>(match);
	}

	// Real per day rates become per second rates. One place, so the time scale keeps every ratio.
	float PerSecond(float perDay, const MatchConfigComponent& config)
	{
		return config.SecondsPerRealDay > 0 ? perDay / config.SecondsPerRealDay : 0;
	}

	const char* GetTankMaterial(const CargoTankComponent& tank)
	{
		if (tank.AmountKg <= 0)
			return "Materials/TankEmpty.material";
		if (tank.Holes > 0)
			return "Materials/TankLeaking.material";
		return tank.Kind == CargoKind::Oxygen ? "Materials/TankOxygen.material" : "Materials/TankWater.material";
	}
}

void LifeSupportSystem::OnStart(World& world)
{
	const LifeSupportConfigComponent config = Cargo::GetConfig(world);
	for (int32 crew = 0; crew < CrewCount; ++crew)
	{
		const Entity entity = world.CreateEntity(String::Format("Crew %s Resources", GetCrewName(crew)));
		TruckResourcesComponent& resources = world.Add<TruckResourcesComponent>(entity);
		resources.Crew = crew;
		resources.CabOxygenKg = config.CabOxygenReserveKg;
		resources.CabWaterKg = config.CabWaterReserveKg;
		SpawnTanks(world, crew);
	}
}

void LifeSupportSystem::SpawnTanks(World& world, int32 crew)
{
	Vec3 bay;
	for (auto [entity, transform, cargoBay] : world.View<TransformComponent, CargoBayComponent>().each())
	{
		if (cargoBay.Crew == crew)
			bay = transform.Position;
	}

	const MatchConfigComponent match = GetMatchConfig(world);
	const TankConfigComponent tanks = Cargo::GetTankConfig(world);
	const int32 total = match.CargoOxygenTanks + match.CargoWaterTanks;
	const int32 perRow = std::max(tanks.PerRow, 1);
	const int32 rows = (total + perRow - 1) / perRow;
	for (int32 i = 0; i < total; ++i)
	{
		const bool oxygen = i < match.CargoOxygenTanks;
		const int32 column = i % perRow;
		const int32 row = i / perRow;
		const Vec3 offset{ (column - (perRow - 1) * 0.5f) * tanks.Spacing, TankScale.Y * 0.5f, (row - (rows - 1) * 0.5f) * tanks.Spacing };

		const Entity entity = world.CreateEntity(String::Format("Crew %s %s Tank %d", GetCrewName(crew), oxygen ? "Oxygen" : "Water", i + 1));
		TransformComponent& transform = world.Get<TransformComponent>(entity);
		transform.Position = bay + offset;
		transform.Scale = TankScale;

		CargoTankComponent& tank = world.Add<CargoTankComponent>(entity);
		tank.Crew = crew;
		tank.Kind = oxygen ? CargoKind::Oxygen : CargoKind::Water;
		tank.CapacityKg = oxygen ? tanks.OxygenTankKg : tanks.WaterTankKg;
		tank.AmountKg = tank.CapacityKg;

		world.Add<MeshComponent>(entity).Mesh = "Cube";
		world.Add<MaterialComponent>(entity).Material = GetTankMaterial(tank);
		ColliderComponent& collider = world.Add<ColliderComponent>(entity);
		collider.Layer = "Cargo";
	}
}

void LifeSupportSystem::Update(World& world, float deltaTime)
{
	if (!IsRacing(world))
		return;
	const MatchConfigComponent match = GetMatchConfig(world);
	const LifeSupportConfigComponent config = Cargo::GetConfig(world);
	const LifeSupportDataComponent data = Cargo::GetData(world);
	const float crew = static_cast<float>(config.CrewAboard);

	for (auto [entity, resources] : world.View<TruckResourcesComponent>().each())
	{
		// Power: the generator charges the battery as well as its health allows, and the scrubber
		// and recycler run off it.
		const bool powered = resources.Power > 0;
		resources.Power += config.PowerGenerationPerSecond * resources.PowerHealth * deltaTime;
		if (powered)
			resources.Power -= config.PowerDrawPerSecond * deltaTime;
		resources.Power = std::clamp(resources.Power, 0.0f, 1.0f);

		// Oxygen: cab reserve first, then the cargo.
		const float breath = crew * PerSecond(data.OxygenKgPerPersonDay, match) * deltaTime;
		const float fromCab = std::min(resources.CabOxygenKg, breath);
		resources.CabOxygenKg -= fromCab;
		float breathed = fromCab;
		const float stillNeeded = breath - fromCab;
		if (stillNeeded > 0)
		{
			if (!resources.TappingCargo)
				FP_LOG("Life support: Crew %s's cab air ran out. Breathing the cargo", GetCrewName(resources.Crew));
			resources.TappingCargo = true;
			const float tapped = Cargo::Draw(world, resources.Crew, CargoKind::Oxygen, stillNeeded);
			resources.OxygenTappedKg += tapped;
			breathed += tapped;
		}
		const bool outOfAir = breathed < breath;
		if (outOfAir && !resources.OutOfAir)
			FP_LOG_WARN("Life support: Crew %s is out of oxygen", GetCrewName(resources.Crew));
		resources.OutOfAir = outOfAir;
		resources.OxygenBreathedKg += breathed;

		// CO2: breathed out into the cab, then scrubbed while there is power.
		const float exhaled = crew * PerSecond(data.CarbonDioxideKgPerPersonDay, match) * deltaTime;
		resources.CabCarbonDioxideKg += exhaled;
		if (powered)
			resources.CabCarbonDioxideKg -= std::min(resources.CabCarbonDioxideKg, exhaled * config.ScrubberCapacity * resources.ScrubberHealth);

		// Water: the recycler wins back a share of what is used, and the rest comes from the cab
		// reserve and then the cargo.
		const float used = crew * PerSecond(data.WaterKgPerPersonDay, match) * deltaTime;
		const float recycled = powered ? used * config.WaterRecovery * resources.RecyclerHealth : 0;
		resources.WaterUsedKg += used;
		resources.WaterRecycledKg += recycled;
		const float spent = used - recycled;
		const float fromCabWater = std::min(resources.CabWaterKg, spent);
		resources.CabWaterKg -= fromCabWater;
		if (spent > fromCabWater)
			Cargo::Draw(world, resources.Crew, CargoKind::Water, spent - fromCabWater);
	}
}

void CargoTankSystem::Update(World& world, float deltaTime)
{
	const bool racing = IsRacing(world);
	const TankConfigComponent tanks = Cargo::GetTankConfig(world);
	const LifeSupportDataComponent data = Cargo::GetData(world);

	for (auto [entity, tank, material] : world.View<CargoTankComponent, MaterialComponent>().each())
	{
		if (racing && tank.Holes > 0 && tank.AmountKg > 0)
		{
			float lost = 0;
			if (tank.Kind == CargoKind::Oxygen)
			{
				// Exact for a constant rate, so a long frame cannot overshoot.
				const float remaining = tank.AmountKg * std::exp(-Cargo::OxygenLeakRate(tanks, data, tank.Holes) * deltaTime);
				lost = tank.AmountKg - remaining;
			}
			else
				lost = std::min(tank.AmountKg, tanks.WaterLeakKgPerSecond * tank.Holes * deltaTime);
			tank.AmountKg -= lost;
			if (tank.Kind == CargoKind::Oxygen)
			{
				if (TruckResourcesComponent* resources = Cargo::FindResources(world, tank.Crew))
					resources->OxygenLeakedKg += lost;
			}
		}

		const char* look = GetTankMaterial(tank);
		if (material.Material != look)
			material.Material = look;
	}
}

void ColonyClockSystem::OnStart(World& world)
{
	const MatchConfigComponent match = GetMatchConfig(world);
	const LifeSupportDataComponent data = Cargo::GetData(world);
	for (auto [entity, colony] : world.View<ColonyComponent>().each())
	{
		ColonyStateComponent& state = world.Add<ColonyStateComponent>(entity);
		const float perDay = match.ColonyCrewSize * data.OxygenKgPerPersonDay;
		state.OxygenKg = perDay * match.ColonyOxygenDays;
		state.OxygenUsePerSecond = PerSecond(perDay, match);
	}
}

void ColonyClockSystem::Update(World& world, float deltaTime)
{
	if (!IsRacing(world))
		return;
	for (auto [entity, state] : world.View<ColonyStateComponent>().each())
		state.OxygenKg = std::max(0.0f, state.OxygenKg - state.OxygenUsePerSecond * deltaTime);
}

void LifeSupportReadoutSystem::Update(World& world, float deltaTime)
{
	const MatchStateComponent* state = GetState(world);
	if (!state)
		return;
	const MatchConfigComponent match = GetMatchConfig(world);
	m_SinceLast += deltaTime;
	const bool due = state->Phase == MatchPhase::Racing && match.StatusLogSeconds > 0 && m_SinceLast >= match.StatusLogSeconds;
	if (!due && !Input::WasKeyPressed(SDL_SCANCODE_TAB))
		return;
	m_SinceLast = 0;

	float colonySeconds = 0;
	for (auto [entity, colony] : world.View<ColonyStateComponent>().each())
		colonySeconds = colony.GetSecondsLeft();

	int32 localCrew = -1;
	const Entity local = FindLocalMember(world);
	if (local != NullEntity)
		localCrew = world.Get<CrewMemberComponent>(local).Crew;

	FP_LOG("Readout %.0f s | colony oxygen lasts %.0f s", state->RaceTime, colonySeconds);
	for (auto [entity, resources] : world.View<TruckResourcesComponent>().each())
	{
		const Cargo::Totals cargo = Cargo::Sum(world, resources.Crew);
		FP_LOG("  Crew %s%s | cab O2 %.1f kg H2O %.1f kg CO2 %.2f kg | cargo O2 %.0f/%.0f kg H2O %.0f/%.0f kg | %d leaking | power %.0f%% | %d mites, %d killed, %.1f kg eaten, %.1f kg recovered",
			GetCrewName(resources.Crew), resources.Crew == localCrew ? " (you)" : "",
			resources.CabOxygenKg, resources.CabWaterKg, resources.CabCarbonDioxideKg,
			cargo.OxygenKg, cargo.OxygenCapacityKg, cargo.WaterKg, cargo.WaterCapacityKg,
			cargo.LeakingTanks, resources.Power * 100, Siphons::CountActive(world, resources.Crew), resources.SiphonsKilled,
			resources.OxygenEatenKg, resources.OxygenRecoveredKg);
	}
}
