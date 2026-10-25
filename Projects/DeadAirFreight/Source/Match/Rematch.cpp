#include "Match/Rematch.h"
#include "Crew/CrewComponents.h"
#include "Engine/Net/NetworkComponent.h"
#include "Engine/SceneSerializer.h"
#include "Match/MatchQueries.h"
#include "Net/GameNet.h"
#include "Title/TitleComponents.h"

using namespace FP;
using namespace MatchQueries;

namespace
{
	// Asked for, waiting for RestartSystem to carry it out.
	Rematch::Mode Requested = Rematch::Mode::None;
	// The restart under way, while systems start again.
	Rematch::Mode Current = Rematch::Mode::None;
	// The game's systems as play began, the ones a restart starts again (RestartSystem first).
	std::vector<RefPtr<System>> GameSystems;
}

namespace Rematch
{
	void RaceAgain(World& world)
	{
		if (!GameNet::IsHost(world))
			return;
		GameNet::Send(world, GameNet::Begin(GameNet::Message::RaceAgain), NetChannel::Reliable);
		Begin(world, Mode::RaceAgain);
	}

	void Begin(World&, Mode mode)
	{
		if (Requested == Mode::None)
			Requested = mode;
	}

	Mode GetMode()
	{
		return Current;
	}
}

void RaceSystemGroup::Update(World& world, float deltaTime)
{
	// Copied first: a system may add another while it runs.
	const std::vector<RefPtr<System>> systems = m_Systems;
	for (const RefPtr<System>& system : systems)
		system->Update(world, deltaTime);
}

void RaceSystemGroup::Clear(World& world)
{
	for (const RefPtr<System>& system : m_Systems)
		system->OnStop(world);
	m_Systems.clear();
}

RaceSystemGroup* RaceSystemGroup::Find(World& world)
{
	for (const RefPtr<System>& system : world.GetSystems())
	{
		if (RaceSystemGroup* group = dynamic_cast<RaceSystemGroup*>(system.Get()))
			return group;
	}
	return nullptr;
}

void RestartSystem::OnStart(World& world)
{
	Requested = Rematch::Mode::None;
	m_Stage = Stage::Idle;

	// The game's systems are this one and everything added after it so far. Systems added later
	// while playing, like the network when someone hosts, are not the game's to restart.
	GameSystems.clear();
	bool found = false;
	for (const RefPtr<System>& system : world.GetSystems())
	{
		found |= system.Get() == this;
		if (found)
			GameSystems.push_back(system);
	}

	m_Scene.clear();
	for (Entity entity : world.GetEntities())
	{
		if (!world.Has<NetworkComponent>(entity))
			m_Scene.push_back(SceneSerializer::SaveEntityToString(world, entity).CStr());
	}
}

void RestartSystem::OnStop(World&)
{
	// Play is over (the editor's Stop): let go of the systems and the saved scene.
	GameSystems.clear();
	m_Scene.clear();
	Requested = Rematch::Mode::None;
	Current = Rematch::Mode::None;
}

void RestartSystem::Update(World& world, float)
{
	if (m_Stage == Stage::Rebuild)
	{
		Rebuild(world);
		m_Stage = Stage::Idle;
		return;
	}
	if (Requested != Rematch::Mode::None)
	{
		TearDown(world);
		// Destroyed entities go at the end of the frame, so the scene comes back on the next one.
		m_Stage = Stage::Rebuild;
	}
}

void RestartSystem::TearDown(World& world)
{
	Current = Requested;
	FP_LOG("Restart: %s", Current == Rematch::Mode::RaceAgain ? "racing again with the same players" : "back to the title screen");

	if (const MatchStateComponent* state = GetState(world))
	{
		for (int32 slot = 0; slot < MaxSlots; ++slot)
		{
			m_SeatTaken[slot] = state->SeatTaken[slot];
			m_SeatOwners[slot] = state->SeatOwners[slot];
		}
	}

	// The game's systems let go first, while everything they hold still exists.
	for (auto it = GameSystems.rbegin(); it != GameSystems.rend(); ++it)
	{
		if (it->Get() != this)
			(*it)->OnStop(world);
	}

	// What the match put on the players.
	for (auto [entity, member] : world.View<CrewMemberComponent>().each())
	{
		if (world.Has<OnFootComponent>(entity))
			world.Remove<OnFootComponent>(entity);
		if (world.Has<StartLookComponent>(entity))
			world.Remove<StartLookComponent>(entity);
	}

	// Everything else that is not the network's goes: what the match made, and the scene's own
	// entities, which come back fresh.
	Array<Entity> doomed;
	for (Entity entity : world.GetEntities())
	{
		if (!world.Has<NetworkComponent>(entity))
			doomed.Add(entity);
	}
	for (Entity entity : doomed)
		world.DestroyEntity(entity);
}

void RestartSystem::Rebuild(World& world)
{
	for (const std::string& text : m_Scene)
	{
		if (SceneSerializer::LoadEntityFromString(world, text) == NullEntity)
			FP_LOG_WARN("Restart: a scene entity could not be put back");
	}

	for (const RefPtr<System>& system : GameSystems)
	{
		if (system.Get() != this)
			system->OnStart(world);
	}

	if (Current == Rematch::Mode::RaceAgain)
	{
		if (MatchStateComponent* state = GetState(world))
		{
			for (int32 slot = 0; slot < MaxSlots; ++slot)
			{
				state->SeatTaken[slot] = m_SeatTaken[slot];
				state->SeatOwners[slot] = m_SeatOwners[slot];
			}
			// The host starts the countdown; everyone else hears it.
			if (GameNet::IsHost(world))
			{
				state->Phase = MatchPhase::Lobby;
				state->StartRequested = true;
			}
		}
	}
	Current = Rematch::Mode::None;
	Requested = Rematch::Mode::None;
}
