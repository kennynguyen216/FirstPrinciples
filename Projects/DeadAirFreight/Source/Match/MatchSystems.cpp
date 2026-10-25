#include "Match/MatchSystems.h"
#include "Audio/Sounds.h"
#include "Crew/CrewComponents.h"
#include "Match/MatchQueries.h"
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

	// Other players walking a bay. Walking is only known to its owner, so it is read from the shared scale.
	bool ShowsCharacter(World& world, Entity entity, const CrewMemberComponent& member)
	{
		return member.Slot >= 0 && entity != FindLocalMember(world) && world.Get<TransformComponent>(entity).Scale.Y > HiddenScale.Y * 10;
	}

	// Stands the character on the body's feet, facing where the body faces.
	void PlaceCharacter(World& world, Entity character, Entity memberEntity, const CrewMemberComponent& member)
	{
		const TransformComponent& body = world.Get<TransformComponent>(memberEntity);
		TransformComponent& transform = world.Get<TransformComponent>(character);
		transform.Position = body.Position - Vec3{ 0, body.Scale.Y * 0.5f, 0 };
		transform.Rotation = { 0, body.Rotation.Y + 90, 0 };
		transform.Scale = Vec3{ 1, 1, 1 } * (body.Scale.Y / CharacterHeight);
		SetLook(world, character, CharacterMesh, "Materials/Characters/Base.material",
			{ "", CharacterCaps[member.Crew], "Materials/Characters/Helmet.material", "Materials/Characters/Denim.material",
				CharacterShirts[member.Crew], "Materials/Characters/Button.material", "Materials/Characters/Belt.material" });
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

		// Playing alone, keys 1 to 4 pick the seat so every role can be tried without four machines.
		int32 players = 0;
		for (int32 i = 0; i < seats.Num(); ++i)
			players += i == 0 || seats[i].Owner != seats[i - 1].Owner ? 1 : 0;
		if (players == 1)
		{
			const Key seatKeys[MaxSlots] = { SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4 };
			for (int32 slot = 0; slot < MaxSlots; ++slot)
			{
				if (Input::WasKeyPressed(seatKeys[slot]) && m_SoloSeat != slot)
				{
					m_SoloSeat = slot;
					FP_LOG("Crew: playing alone as P%d, Crew %s %s", slot + 1, GetCrewName(slot / SlotsPerCrew), GetRoleName(slot % SlotsPerCrew == 0 ? CrewRole::Driver : CrewRole::Defender));
				}
			}
		}

		int32 next = players == 1 ? m_SoloSeat : 0;
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
