#pragma once

#include "Match/MatchComponents.h"

// Seats players as P1..P4 and gives each a crew and a role: P1/P2 are Crew A's Driver and
// Defender, P3/P4 are Crew B's. Seats are ordered by peer id, which every peer sees the same,
// so all of them agree without sending anything. Seats lock once the countdown starts.
class CrewSystem : public FP::System
{
public:
	const char* GetName() const override { return "Crew"; }
	void Update(FP::World& world, float deltaTime) override;

private:
	FP::int32 m_SoloSeat = 0;
};

// Lobby -> Countdown -> Racing -> Results. Places the local player at the start when the
// countdown begins and ends the race when a truck reaches the colony.
class MatchSystem : public FP::System
{
public:
	const char* GetName() const override { return "Match"; }
	void OnStart(FP::World& world) override;
	void Update(FP::World& world, float deltaTime) override;

private:
	void StartCountdown(FP::World& world, MatchStateComponent& state, const MatchConfigComponent& config);
	void PlaceLocalMember(FP::World& world);
	// Hands a crew's whole cargo to the colony and scores it.
	void Deliver(FP::World& world, MatchStateComponent& state, const MatchConfigComponent& config, FP::int32 crew, FP::Entity colony);
	void Finish(MatchStateComponent& state, FP::int32 winner);
};
