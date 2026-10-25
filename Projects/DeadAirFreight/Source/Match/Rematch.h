#pragma once

#include "Engine/Engine.h"
#include "Match/MatchComponents.h"

#include <string>
#include <vector>

// Starting over after a race without leaving play, so the network session and the players in it stay.
// A restart clears everything the match made, puts the scene back as it was when play began and
// starts Dead Air Freight's systems again. The engine's systems, the network among them, carry on.
namespace Rematch
{
	enum class Mode
	{
		None,
		RaceAgain, // the same players in the same seats, straight into a new countdown
		Title,     // back to the title screen, having left the game
	};

	// On the host: everyone races again. Tells the others, who restart with it.
	void RaceAgain(FP::World& world);
	// Starts over on this machine, for the reason given.
	void Begin(FP::World& world, Mode mode);
	// What the restart in progress is for, so systems starting again know where to pick up, or None
	// when play has just begun.
	Mode GetMode();
}

// Systems that only run during a race, added when it starts (the Driver's, and the bridge to them).
// The engine cannot take a system back, so they live in here, where a restart can empty them.
class RaceSystemGroup : public FP::System
{
public:
	const char* GetName() const override { return "Race Systems"; }
	void Update(FP::World& world, float deltaTime) override;
	void OnStop(FP::World& world) override { Clear(world); }

	template <typename T, typename... Args>
	T* Add(FP::World& world, Args&&... args)
	{
		FP::RefPtr<T> system = FP::MakeRef<T>(std::forward<Args>(args)...);
		m_Systems.push_back(system);
		system->OnStart(world);
		return system.Get();
	}
	void Clear(FP::World& world);

	static RaceSystemGroup* Find(FP::World& world);

private:
	std::vector<FP::RefPtr<FP::System>> m_Systems;
};

// Added first of the game's systems. Remembers the scene when play begins and carries out restarts.
class RestartSystem : public FP::System
{
public:
	const char* GetName() const override { return "Restart"; }
	void OnStart(FP::World& world) override;
	void Update(FP::World& world, float deltaTime) override;
	void OnStop(FP::World& world) override;

private:
	enum class Stage { Idle, Rebuild };

	void TearDown(FP::World& world);
	void Rebuild(FP::World& world);

	Stage m_Stage = Stage::Idle;
	// The scene's own entities as play began, saved as text, except networked ones, which the
	// network looks after.
	std::vector<std::string> m_Scene;
	// Seats carried over a race-again restart.
	bool m_SeatTaken[MaxSlots] = {};
	FP::uint64 m_SeatOwners[MaxSlots] = {};
};
