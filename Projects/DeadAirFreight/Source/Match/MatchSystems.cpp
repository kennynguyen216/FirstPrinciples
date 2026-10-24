#include "Match/MatchSystems.h"
#include "Match/MatchQueries.h"
#include "LifeSupport/Cargo.h"
#include "Physics/PhysicsComponents.h"
#include "Truck/TruckComponents.h"

#include <algorithm>
#include <cmath>

using namespace FP;
using namespace MatchQueries;

namespace
{
	struct Seat
	{
		uint64 Owner = 0;
		Entity Member = NullEntity;
	};

	const char* TruckMaterials[CrewCount] = { "Materials/TruckCrewA.material", "Materials/TruckCrewB.material" };

	// Placeholder looks until the real models are in: a box in the crew's color for a truck and a
	// white post for a Defender. Every peer works these out for every player, so nothing is sent.
	void ApplyLook(World& world, Entity entity, const CrewMemberComponent& member, const TruckConfigComponent& truck)
	{
		const bool driver = member.Slot >= 0 && member.Role == CrewRole::Driver;
		const char* material = driver ? TruckMaterials[member.Crew] : "Materials/Defender.material";
		MaterialComponent* look = world.TryGet<MaterialComponent>(entity);
		if (!look)
			look = &world.Add<MaterialComponent>(entity);
		if (look->Material != material)
			look->Material = material;

		// Trucks hit things in space. Defenders are people in the bay.
		ColliderComponent* collider = world.TryGet<ColliderComponent>(entity);
		if (!collider)
			collider = &world.Add<ColliderComponent>(entity);
		collider->Layer = driver ? "Truck" : "Player";

		// Scale is shared over the network, so only the owner sets it.
		if (!IsRemote(world, entity))
		{
			TransformComponent& transform = world.Get<TransformComponent>(entity);
			transform.Scale = driver ? truck.Size : member.Slot >= 0 ? Vec3{ 0.8f, 1.8f, 0.8f } : Vec3{ 0.3f, 0.3f, 0.3f };
		}
	}
}

void CrewSystem::Update(World& world, float)
{
	const MatchStateComponent* state = GetState(world);
	const bool locked = state && state->Phase != MatchPhase::Lobby;

	if (!locked)
	{
		Array<Seat> seats;
		for (auto [entity, member] : world.View<CrewMemberComponent>().each())
			seats.Add(Seat{ GetOwner(world, entity), entity });
		seats.Sort([](const Seat& a, const Seat& b) { return a.Owner < b.Owner; });

		int32 next = 0;
		uint64 previousOwner = 0;
		for (int32 i = 0; i < seats.Num(); ++i)
		{
			CrewMemberComponent& member = world.Get<CrewMemberComponent>(seats[i].Member);
			// The swapped out network template shares our owner for one frame. Seat only one.
			const bool duplicate = i > 0 && seats[i].Owner == previousOwner;
			previousOwner = seats[i].Owner;
			if (duplicate || next >= MaxSlots)
			{
				member.Slot = -1;
				member.Crew = -1;
				continue;
			}
			member.Slot = next++;
			member.Crew = member.Slot / SlotsPerCrew;
			member.Role = member.Slot % SlotsPerCrew == 0 ? CrewRole::Driver : CrewRole::Defender;
		}
	}

	const Entity match = FindMatch(world);
	const TruckConfigComponent* truck = match == NullEntity ? nullptr : world.TryGet<TruckConfigComponent>(match);
	const TruckConfigComponent defaults;
	for (auto [entity, member] : world.View<CrewMemberComponent>().each())
		ApplyLook(world, entity, member, truck ? *truck : defaults);
}

void MatchSystem::OnStart(World& world)
{
	const Entity match = FindMatch(world);
	if (match == NullEntity)
	{
		FP_LOG_WARN("Match: the scene has no entity with a MatchConfig component, so the match cannot run");
		return;
	}
	world.Add<MatchStateComponent>(match);
	FP_LOG("Match: lobby open. Waiting for players, or press Enter to start now");
}

void MatchSystem::Update(World& world, float deltaTime)
{
	const Entity match = FindMatch(world);
	if (match == NullEntity)
		return;
	MatchStateComponent* state = world.TryGet<MatchStateComponent>(match);
	if (!state)
		return;
	const MatchConfigComponent& config = world.Get<MatchConfigComponent>(match);

	switch (state->Phase)
	{
		case MatchPhase::Lobby:
		{
			int32 seated = 0;
			for (auto [entity, member] : world.View<CrewMemberComponent>().each())
				seated += member.Slot >= 0 ? 1 : 0;
			// Wait out the first moments so the network has swapped in this peer's own player.
			if (world.GetTime() < 0.5 || seated == 0)
				break;
			if (seated >= config.PlayersRequired || Input::WasKeyPressed(SDL_SCANCODE_RETURN))
				StartCountdown(world, *state, config);
			break;
		}
		case MatchPhase::Countdown:
		{
			const int32 before = static_cast<int32>(std::ceil(state->Countdown));
			state->Countdown -= deltaTime;
			const int32 after = static_cast<int32>(std::ceil(state->Countdown));
			if (state->Countdown <= 0)
			{
				state->Phase = MatchPhase::Racing;
				FP_LOG("Match: GO");
			}
			else if (after != before)
				FP_LOG("Match: %d", after);
			break;
		}
		case MatchPhase::Racing:
		{
			state->RaceTime += deltaTime;
			for (auto [colonyEntity, colonyTransform, colony] : world.View<TransformComponent, ColonyComponent>().each())
			{
				for (auto [entity, transform, member] : world.View<TransformComponent, CrewMemberComponent>().each())
				{
					if (member.Slot < 0 || member.Role != CrewRole::Driver || state->Crews[member.Crew].Arrived)
						continue;
					if ((transform.Position - colonyTransform.Position).Length() <= colony.ArrivalRadius)
						Deliver(world, *state, config, member.Crew, colonyEntity);
				}
			}

			const ColonyStateComponent* colony = nullptr;
			for (auto [entity, colonyState] : world.View<ColonyStateComponent>().each())
				colony = &colonyState;
			if (colony && colony->OxygenKg <= 0)
			{
				FP_LOG_WARN("Match: the colony ran out of oxygen. Everybody loses");
				Finish(*state, -1);
				break;
			}

			// Over once every crew with a truck has arrived.
			bool waiting = false;
			for (int32 crew = 0; crew < CrewCount; ++crew)
				waiting |= FindDriver(world, crew) != NullEntity && !state->Crews[crew].Arrived;
			if (!waiting)
			{
				int32 winner = -1;
				for (int32 crew = 0; crew < CrewCount; ++crew)
				{
					if (state->Crews[crew].Arrived && (winner < 0 || state->Crews[crew].Score > state->Crews[winner].Score))
						winner = crew;
				}
				Finish(*state, winner);
			}
			break;
		}
		case MatchPhase::Results:
			break;
	}
}

void MatchSystem::StartCountdown(World& world, MatchStateComponent& state, const MatchConfigComponent& config)
{
	state.Phase = MatchPhase::Countdown;
	state.Countdown = config.CountdownSeconds;

	for (auto [entity, member] : world.View<CrewMemberComponent>().each())
	{
		if (member.Slot >= 0)
			FP_LOG("Match: P%d is Crew %s %s%s", member.Slot + 1, GetCrewName(member.Crew), GetRoleName(member.Role), IsRemote(world, entity) ? "" : " (you)");
	}
	if (FindDriver(world, 0) == NullEntity || FindDriver(world, 1) == NullEntity)
		FP_LOG_WARN("Match: starting without a Driver for both crews");
	PlaceLocalMember(world);
	FP_LOG("Match: countdown %.0f", config.CountdownSeconds);
}

void MatchSystem::PlaceLocalMember(World& world)
{
	const Entity local = FindLocalMember(world);
	if (local == NullEntity)
		return;
	CrewMemberComponent& member = world.Get<CrewMemberComponent>(local);
	TransformComponent& transform = world.Get<TransformComponent>(local);
	member.Velocity = {};
	if (member.Slot < 0)
		return;

	if (member.Role == CrewRole::Defender)
	{
		// Until the bay is playable (M3) the Defender waits in it and watches their truck.
		for (auto [entity, bayTransform, bay] : world.View<TransformComponent, CargoBayComponent>().each())
		{
			if (bay.Crew == member.Crew)
				transform.Position = bayTransform.Position;
		}
		return;
	}

	Vec3 depot;
	float spacing = 12;
	for (auto [entity, depotTransform, depotInfo] : world.View<TransformComponent, DepotComponent>().each())
	{
		depot = depotTransform.Position;
		spacing = depotInfo.StartSpacing;
	}
	Vec3 colony{ 0, 0, -1 };
	for (auto [entity, colonyTransform, colonyInfo] : world.View<TransformComponent, ColonyComponent>().each())
		colony = colonyTransform.Position;

	const float yaw = YawToward(depot, colony);
	transform.Rotation = { 0, yaw, 0 };
	const Vec3 side = transform.GetRight();
	transform.Position = depot + side * (member.Crew == 0 ? -spacing * 0.5f : spacing * 0.5f);
}

void MatchSystem::Deliver(World& world, MatchStateComponent& state, const MatchConfigComponent& config, int32 crew, Entity colony)
{
	CrewResult& result = state.Crews[crew];
	result.Arrived = true;
	result.ArrivalTime = state.RaceTime;
	result.OxygenDeliveredKg = Cargo::Draw(world, crew, CargoKind::Oxygen, 1e9f);
	result.WaterDeliveredKg = Cargo::Draw(world, crew, CargoKind::Water, 1e9f);
	const float timeBonus = std::max(0.0f, config.ParTimeSeconds - result.ArrivalTime) * config.TimeBonusPerSecond;
	result.Score = result.OxygenDeliveredKg + result.WaterDeliveredKg + timeBonus;

	if (ColonyStateComponent* colonyState = world.TryGet<ColonyStateComponent>(colony))
	{
		colonyState->OxygenKg += result.OxygenDeliveredKg;
		colonyState->WaterReceivedKg += result.WaterDeliveredKg;
	}
	FP_LOG("Match: Crew %s docked at %.1f s with %.0f kg oxygen and %.0f kg water. Score %.0f (time bonus %.0f)",
		GetCrewName(crew), result.ArrivalTime, result.OxygenDeliveredKg, result.WaterDeliveredKg, result.Score, timeBonus);
}

void MatchSystem::Finish(MatchStateComponent& state, int32 winner)
{
	state.Phase = MatchPhase::Results;
	state.WinnerCrew = winner;
	if (winner >= 0)
		FP_LOG("Match: Crew %s wins with %.0f points", GetCrewName(winner), state.Crews[winner].Score);
	else
		FP_LOG("Match: no winner");
}
