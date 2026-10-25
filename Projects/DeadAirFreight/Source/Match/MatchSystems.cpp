#include "Match/MatchSystems.h"
#include "Audio/Sounds.h"
#include "Crew/CrewComponents.h"
#include "Effects/FreeCam.h"
#include "Match/MatchQueries.h"
#include "Net/GameNet.h"
#include "LifeSupport/Cargo.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <vector>

using namespace FP;
using namespace MatchQueries;

namespace
{
	struct Seat
	{
		uint64 Owner = 0;
		Entity Member = NullEntity;
	};

	// Hidden things shrink to almost nothing. The engine refuses entities scaled to zero, and players
	// are sent to each other as entities.
	const Vec3 HiddenScale{ 0.001f, 0.001f, 0.001f };

	// The crew character: sections Base, Cap, Helmet, Denim, Shirt, Button, Belt. Its feet are at 0
	// and it faces +X. Both crews share the purple model; the orange one differs only in color.
	constexpr const char* CharacterMesh = "Meshes/Characters/PurpleCharacter.obj";
	constexpr float CharacterHeight = 5.02f;
	const char* CharacterCaps[CrewCount] = { "Materials/Characters/OrangeCap.material", "Materials/Characters/PurpleShirt.material" };
	const char* CharacterShirts[CrewCount] = { "Materials/Characters/OrangeShirt.material", "Materials/Characters/PurpleShirt.material" };

	// A walking player's character lives on an entity of its own, because the crew member's scale is
	// the body's size for the game and the model needs a uniform one. Made on every peer, never sent.
	struct CrewCharacterComponent
	{
		Entity Member = NullEntity;
	};

	// Sets a mesh and its materials, one per section; an empty slot uses the first material.
	void SetLook(World& world, Entity entity, const char* mesh, const char* material, std::initializer_list<const char*> slots)
	{
		MeshComponent* shape = world.TryGet<MeshComponent>(entity);
		if (!shape)
			shape = &world.Add<MeshComponent>(entity);
		if (shape->Mesh != mesh)
			shape->Mesh = mesh;
		MaterialComponent* look = world.TryGet<MaterialComponent>(entity);
		if (!look)
			look = &world.Add<MaterialComponent>(entity);
		if (look->Material != material)
			look->Material = material;
		Array<String> wanted;
		for (const char* slot : slots)
			wanted.Add(slot);
		if (!(look->Slots == wanted))
			look->Slots = wanted;
	}

	// A white post in the lobby. Once seated the crew entity is the player's body in the bay, drawn
	// as their crew's character, and it hides while they are up in the cab.
	void ApplyLook(World& world, Entity entity, const CrewMemberComponent& member)
	{
		if (member.Slot < 0)
			SetLook(world, entity, "Cube", "Materials/Defender.material", {});
		else if (world.Has<MeshComponent>(entity))
			world.Remove<MeshComponent>(entity);

		// Scale is shared over the network, so only the owner sets it.
		if (!IsRemote(world, entity))
		{
			TransformComponent& transform = world.Get<TransformComponent>(entity);
			transform.Scale = member.Slot < 0 ? Vec3{ 0.3f, 0.3f, 0.3f } : world.Has<OnFootComponent>(entity) ? WalkerSize : HiddenScale;
		}
	}

	// Other players walking a bay, and this one too while the trailer camera is on. Walking is only
	// known to its owner, so it is read from the shared scale.
	bool ShowsCharacter(World& world, Entity entity, const CrewMemberComponent& member)
	{
		return member.Slot >= 0 && (entity != FindLocalMember(world) || FreeCam::IsOn()) && world.Get<TransformComponent>(entity).Scale.Y > HiddenScale.Y * 10;
	}

	// Stands the character on the body's feet, facing where the body faces.
	void PlaceCharacter(World& world, Entity character, Entity memberEntity, const CrewMemberComponent& member)
	{
		const TransformComponent& body = world.Get<TransformComponent>(memberEntity);
		TransformComponent& transform = world.Get<TransformComponent>(character);
		transform.Position = body.Position - Vec3{ 0, body.Scale.Y * 0.5f, 0 };
		transform.Rotation = { 0, body.Rotation.Y + 90, 0 };
		DressCrewCharacter(world, character, member.Crew);
		transform.Scale = Vec3{ 1, 1, 1 } * (body.Scale.Y / CharacterHeight);
	}

	void UpdateCharacters(World& world)
	{
		std::vector<Entity> drawn;
		std::vector<Entity> stale;
		for (auto [entity, character] : world.View<CrewCharacterComponent>().each())
		{
			const CrewMemberComponent* member = world.IsValid(character.Member) ? world.TryGet<CrewMemberComponent>(character.Member) : nullptr;
			if (!member || !ShowsCharacter(world, character.Member, *member))
			{
				stale.push_back(entity);
				continue;
			}
			drawn.push_back(character.Member);
			PlaceCharacter(world, entity, character.Member, *member);
		}
		for (Entity entity : stale)
			world.DestroyEntity(entity);

		for (auto [entity, member] : world.View<CrewMemberComponent>().each())
		{
			if (!ShowsCharacter(world, entity, member) || std::find(drawn.begin(), drawn.end(), entity) != drawn.end())
				continue;
			const Entity character = world.CreateEntity("Crew Character");
			world.Add<CrewCharacterComponent>(character).Member = entity;
			PlaceCharacter(world, character, entity, member);
		}
	}
}

void DressCrewCharacter(World& world, Entity entity, int32 crew)
{
	SetLook(world, entity, CharacterMesh, "Materials/Characters/Base.material",
		{ "", CharacterCaps[crew], "Materials/Characters/Helmet.material", "Materials/Characters/Denim.material",
			CharacterShirts[crew], "Materials/Characters/Button.material", "Materials/Characters/Belt.material" });
	world.Get<TransformComponent>(entity).Scale = Vec3{ 1, 1, 1 } * (WalkerSize.Y / CharacterHeight);
}

void CrewSystem::Update(World& world, float)
{
	const MatchStateComponent* state = GetState(world);
	// Seats settle in the lobby, and on the title screen, and lock once the countdown starts.
	const bool locked = state && state->Phase != MatchPhase::Lobby && state->Phase != MatchPhase::Title;

	if (!locked)
	{
		Array<Seat> seats;
		for (auto [entity, member] : world.View<CrewMemberComponent>().each())
			seats.Add(Seat{ GetOwner(world, entity), entity });
		seats.Sort([](const Seat& a, const Seat& b) { return a.Owner < b.Owner; });

		Array<uint64> owners;
		for (const Seat& seat : seats)
		{
			if (owners.IsEmpty() || owners[owners.Num() - 1] != seat.Owner)
				owners.Add(seat.Owner);
		}

		// The host fills the seats; everyone else hears them in the match state (NetSyncSystem).
		MatchStateComponent* seating = GetState(world);
		if (seating && GameNet::IsHost(world))
		{
			// Playing alone, keys 1 to 4 pick the seat so every role can be tried without four
			// machines. Only in the lobby, so typing an address on the title screen picks nothing.
			if (owners.Num() == 1 && seating->Phase == MatchPhase::Lobby)
			{
				const Key seatKeys[MaxSlots] = { SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4 };
				for (int32 slot = 0; slot < MaxSlots; ++slot)
				{
					if (Input::WasKeyPressed(seatKeys[slot]) && m_SoloSeat != slot)
					{
						m_SoloSeat = slot;
						FP_LOG("Crew: playing alone as P%d, Crew %s %s", slot + 1, GetCrewName(GetSlotCrew(slot)), GetRoleName(slot < CrewCount ? CrewRole::Driver : CrewRole::Defender));
					}
				}
			}
			// Players who left give up their seats.
			for (int32 slot = 0; slot < MaxSlots; ++slot)
			{
				bool here = false;
				for (uint64 owner : owners)
					here |= owner == seating->SeatOwners[slot];
				if (seating->SeatTaken[slot] && !here)
					seating->SeatTaken[slot] = false;
			}
			if (owners.Num() == 1)
			{
				for (bool& taken : seating->SeatTaken)
					taken = false;
				seating->SeatTaken[m_SoloSeat] = true;
				seating->SeatOwners[m_SoloSeat] = owners[0];
			}
			else
			{
				// Newcomers take the first free seat, so everyone already seated keeps theirs.
				for (uint64 owner : owners)
				{
					bool seated = false;
					for (int32 slot = 0; slot < MaxSlots; ++slot)
						seated |= seating->SeatTaken[slot] && seating->SeatOwners[slot] == owner;
					for (int32 slot = 0; slot < MaxSlots && !seated; ++slot)
					{
						if (!seating->SeatTaken[slot])
						{
							seating->SeatTaken[slot] = true;
							seating->SeatOwners[slot] = owner;
							seated = true;
						}
					}
				}
			}
		}

		for (const Seat& seat : seats)
		{
			CrewMemberComponent& member = world.Get<CrewMemberComponent>(seat.Member);
			member.Slot = -1;
			member.Crew = -1;
		}
		for (int32 slot = 0; seating && slot < MaxSlots; ++slot)
		{
			if (!seating->SeatTaken[slot])
				continue;
			// The swapped out network template shares its owner for one frame. Seat only one.
			for (const Seat& seat : seats)
			{
				CrewMemberComponent& member = world.Get<CrewMemberComponent>(seat.Member);
				if (seat.Owner != seating->SeatOwners[slot] || member.Slot >= 0)
					continue;
				member.Slot = slot;
				member.Crew = GetSlotCrew(slot);
				member.Role = slot < CrewCount ? CrewRole::Driver : CrewRole::Defender;
				break;
			}
		}
	}

	for (auto [entity, member] : world.View<CrewMemberComponent>().each())
		ApplyLook(world, entity, member);
	UpdateCharacters(world);
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
	FP_LOG("Match: lobby open. Waiting for players, or press Enter to start now. Alone, 1 to 4 picks your seat");
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

	// Away from the host the phases arrive from it (NetSyncSystem). Only take up a starting spot
	// when its countdown begins.
	if (!GameNet::IsHost(world))
	{
		if (state->Phase == MatchPhase::Countdown && m_LastPhase == MatchPhase::Lobby)
			PlaceLocalMember(world);
		m_LastPhase = state->Phase;
		return;
	}
	const MatchPhase lastPhase = m_LastPhase;
	m_LastPhase = state->Phase;

	switch (state->Phase)
	{
		case MatchPhase::Lobby:
		{
			// The wait for players starts over each time a lobby opens.
			if (lastPhase != MatchPhase::Lobby)
				state->Countdown = config.LobbySeconds;
			int32 seated = 0;
			for (auto [entity, member] : world.View<CrewMemberComponent>().each())
				seated += member.Slot >= 0 ? 1 : 0;
			// Wait out the first moments so the network has swapped in this peer's own player.
			if (world.GetTime() < 0.5 || seated == 0)
				break;
			state->Countdown -= deltaTime;
			if (state->Countdown <= 0 || seated >= MaxSlots || Input::WasKeyPressed(SDL_SCANCODE_RETURN) || state->StartRequested)
				StartCountdown(world, *state, config);
			state->StartRequested = false;
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
				Sounds::Play(world, SoundCue::Go);
			}
			else if (after != before)
			{
				FP_LOG("Match: %d", after);
				Sounds::Play(world, SoundCue::Countdown);
			}
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

			// Over once every crew with a truck has arrived. With no trucks at all, such as a
			// Defender playing alone, only the colony clock ends it.
			bool anyTruck = false;
			bool waiting = false;
			for (int32 crew = 0; crew < CrewCount; ++crew)
			{
				const bool hasTruck = FindDriver(world, crew) != NullEntity;
				anyTruck |= hasTruck;
				waiting |= hasTruck && !state->Crews[crew].Arrived;
			}
			if (anyTruck && !waiting)
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
		case MatchPhase::Title:
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
	Sounds::Play(world, SoundCue::Countdown);
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
		// In the aisle in front of the tanks, facing them. The body's middle is half its height up.
		Vec3 bayPosition;
		InteriorComponent bay;
		transform.Scale = WalkerSize;
		if (FindInterior(world, member.Crew, "Bay", &bayPosition, &bay) != NullEntity)
			transform.Position = bayPosition + Vec3{ 0, WalkerSize.Y * 0.5f, bay.HalfLength * 0.6f };
		transform.Rotation = {};
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
