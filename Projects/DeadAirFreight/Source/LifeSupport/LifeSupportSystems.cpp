#include "LifeSupport/LifeSupportSystems.h"
#include "LifeSupport/Cargo.h"
#include "Enemies/SiphonSystems.h"
#include "Match/MatchQueries.h"
#include "Net/GameNet.h"

#include <algorithm>
#include <cmath>

using namespace FP;
using namespace MatchQueries;

namespace
{
	// A pressurized tank 1.2 m across and 2 m tall, domed on top. Just under eye height, so the
	// Defender can see a Mite perched on any of them.
	const Vec3 TankScale{ 1.2f, 2.0f, 1.2f };
	constexpr const char* TankMesh = "Meshes/Tank/Tank.obj";

	// Where tanks stand in the bay, in meters from its middle: x across (the airlock wall is -x),
	// z along (the hatch is +z). Stowed the way a crew would: packed against both walls between the
	// ribs, two deep in places, and a few clumps out on the floor. The airlock, the Defender's
	// start and the walk to the hatch stay clear. Oxygen fills them first, so the oxygen is on the
	// left and up front, and the water is on the right.
	constexpr float Wall = 8.95f; // a tank touching the wall, clear of the handrail
	constexpr float Second = 7.75f; // a tank in front of that one
	constexpr float Pack = 1.3f; // tanks side by side
	constexpr float Rise = Pack * 0.866f; // the next row of a tight clump
	const Vec2 TankSpots[] = {
		// Left wall, front: two deep.
		{ -Wall, -16.5f }, { -Wall, -16.5f + Pack }, { -Wall, -16.5f + 2 * Pack }, { -Wall, -16.5f + 3 * Pack },
		{ -Second, -15.85f }, { -Second, -15.85f + Pack },
		// Right wall, front: a long row with a second one started.
		{ Wall, -16.0f }, { Wall, -16.0f + Pack }, { Wall, -16.0f + 2 * Pack }, { Wall, -16.0f + 3 * Pack }, { Wall, -16.0f + 4 * Pack },
		{ Second, -15.35f }, { Second, -15.35f + Pack },
		// A round clump on the floor, left of the aisle.
		{ -2.5f, -9.0f }, { -2.5f + Pack, -9.0f }, { -2.5f - Pack, -9.0f }, { -2.5f + Pack * 0.5f, -9.0f + Rise },
		{ -2.5f - Pack * 0.5f, -9.0f + Rise }, { -2.5f + Pack * 0.5f, -9.0f - Rise }, { -2.5f - Pack * 0.5f, -9.0f - Rise },
		// Left wall, behind the airlock: two deep.
		{ -Wall, 5.0f }, { -Wall, 5.0f + Pack }, { -Wall, 5.0f + 2 * Pack }, { -Wall, 5.0f + 3 * Pack },
		{ -Second, 5.65f }, { -Second, 5.65f + Pack },
		// Left wall, ahead of the airlock: a short row.
		{ -Wall, -8.5f }, { -Wall, -8.5f + Pack }, { -Wall, -8.5f + 2 * Pack },
		// Left wall, back by the hatch: two deep.
		{ -Wall, 13.5f }, { -Wall, 13.5f + Pack }, { -Wall, 13.5f + 2 * Pack }, { -Wall, 13.5f + 3 * Pack },
		{ -Second, 14.15f }, { -Second, 14.15f + Pack },
		// A loose clump on the open floor up front.
		{ 2.0f, -14.5f }, { 2.0f + Pack, -14.5f }, { 2.0f + Pack * 0.5f, -14.5f + Rise }, { 2.0f + Pack * 0.5f, -14.5f - Rise },
		{ 2.0f - Pack * 0.5f, -14.5f - Rise },
		// Water from here on.
		// Right wall, middle: two deep.
		{ Wall, -4.0f }, { Wall, -4.0f + Pack }, { Wall, -4.0f + 2 * Pack }, { Wall, -4.0f + 3 * Pack },
		{ Second, -3.35f }, { Second, -3.35f + Pack },
		// Right wall, behind the middle: a short row.
		{ Wall, 6.0f }, { Wall, 6.0f + Pack }, { Wall, 6.0f + 2 * Pack },
		// A small clump on the floor, right of the aisle.
		{ 2.5f, -1.0f }, { 2.5f + Pack, -1.0f }, { 2.5f + Pack * 0.5f, -1.0f + Rise }, { 2.5f + Pack * 0.5f, -1.0f - Rise },
		// Right wall, back by the hatch: a row with one in front.
		{ Wall, 12.5f }, { Wall, 12.5f + Pack }, { Wall, 12.5f + 2 * Pack }, { Wall, 12.5f + 3 * Pack },
		{ Second, 13.15f },
		// A pair left out on the floor, right of the aisle.
		{ 4.6f, 4.2f }, { 5.8f, 4.8f },
	};
	constexpr int32 TankSpotCount = static_cast<int32>(sizeof(TankSpots) / sizeof(TankSpots[0]));

	// A few centimeters off true, the same on every machine, so the rows do not look ruled.
	Vec2 Nudge(int32 index)
	{
		const uint32 hash = static_cast<uint32>(index + 1) * 2654435761u;
		const float x = static_cast<float>(hash & 0xFFFF) / 0xFFFF - 0.5f;
		const float z = static_cast<float>(hash >> 16) / 0xFFFF - 0.5f;
		return { x * 0.08f, z * 0.08f };
	}

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
	FindInterior(world, crew, "Bay", &bay);

	const MatchConfigComponent match = GetMatchConfig(world);
	const TankConfigComponent tanks = Cargo::GetTankConfig(world);
	const int32 total = match.CargoOxygenTanks + match.CargoWaterTanks;
	const int32 perRow = std::max(tanks.PerRow, 1);
	for (int32 i = 0; i < total; ++i)
	{
		const bool oxygen = i < match.CargoOxygenTanks;
		Vec2 spot;
		if (i < TankSpotCount)
			spot = TankSpots[i] + Nudge(i);
		else
		{
			// More tanks than spots: rows across the open floor at the front.
			const int32 extra = i - TankSpotCount;
			spot = { (extra % perRow - (perRow - 1) * 0.5f) * tanks.Spacing, -12.5f + (extra / perRow) * tanks.Spacing };
		}
		const Vec3 offset{ spot.X, TankScale.Y * 0.5f, spot.Y };

		const Entity entity = world.CreateEntity(String::Format("Crew %s %s Tank %d", GetCrewName(crew), oxygen ? "Oxygen" : "Water", i + 1));
		TransformComponent& transform = world.Get<TransformComponent>(entity);
		transform.Position = bay + offset;
		transform.Scale = TankScale;

		CargoTankComponent& tank = world.Add<CargoTankComponent>(entity);
		tank.Crew = crew;
		tank.Index = i;
		tank.Kind = oxygen ? CargoKind::Oxygen : CargoKind::Water;
		tank.CapacityKg = oxygen ? tanks.OxygenTankKg : tanks.WaterTankKg;
		tank.AmountKg = tank.CapacityKg;

		world.Add<MeshComponent>(entity).Mesh = TankMesh;
		world.Add<MaterialComponent>(entity).Material = GetTankMaterial(tank);
		// A solid box about the round tank's size, so shots and walkers stop at it.
		world.Add<ColliderComponent>(entity).Size = { 0.85f, 1, 0.85f };
	}
}

void LifeSupportSystem::Update(World& world, float deltaTime)
{
	if (!IsRacing(world) || !GameNet::IsHost(world))
		return;
	const MatchConfigComponent match = GetMatchConfig(world);
	const LifeSupportConfigComponent config = Cargo::GetConfig(world);
	const LifeSupportDataComponent data = Cargo::GetData(world);
	const float crew = static_cast<float>(config.CrewAboard);

	for (auto [entity, resources] : world.View<TruckResourcesComponent>().each())
	{
		if (resources.DriverCab)
			continue;

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
	// Leaks run on the host. Every machine colors its tanks from what they hold.
	const bool racing = IsRacing(world) && GameNet::IsHost(world);
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
	if (!IsRacing(world) || !GameNet::IsHost(world))
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
		FP_LOG("  Crew %s%s | cab O2 %.1f kg H2O %.1f kg CO2 %.2f kg | cargo O2 %.0f/%.0f kg H2O %.0f/%.0f kg | %d leaking | power %.0f%% | %d mites, %d killed, %d flushed, %.1f kg eaten, %.1f kg recovered",
			GetCrewName(resources.Crew), resources.Crew == localCrew ? " (you)" : "",
			resources.CabOxygenKg, resources.CabWaterKg, resources.CabCarbonDioxideKg,
			cargo.OxygenKg, cargo.OxygenCapacityKg, cargo.WaterKg, cargo.WaterCapacityKg,
			cargo.LeakingTanks, resources.Power * 100, Siphons::CountActive(world, resources.Crew), resources.SiphonsKilled, resources.SiphonsFlushed,
			resources.OxygenEatenKg, resources.OxygenRecoveredKg);
	}
}
