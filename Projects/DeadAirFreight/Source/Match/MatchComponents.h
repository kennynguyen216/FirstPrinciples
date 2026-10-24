#pragma once

#include "Engine/Engine.h"

inline constexpr FP::int32 CrewCount = 2;
inline constexpr FP::int32 SlotsPerCrew = 2;
inline constexpr FP::int32 MaxSlots = CrewCount * SlotsPerCrew;

// Tuning for one match, edited on the scene's Match entity. Real world constants (consumption
// rates, flare classes, radiation doses) do not go here. They live on the Real Data entity and in
// Content/Data, where their sources are cited.
struct MatchConfigComponent
{
	// The lobby starts the countdown once this many players are in. Enter starts it early for testing.
	FP::int32 PlayersRequired = 4;
	float CountdownSeconds = 3;
	// Game seconds per real day. Converts the real per day rates to per second rates while keeping
	// their ratios.
	float SecondsPerRealDay = 10;
	// The colony's crew, and how many real days of oxygen it has left when the race starts. Together
	// they set the colony clock, which is also the longest a match can last.
	FP::int32 ColonyCrewSize = 6;
	float ColonyOxygenDays = 30;
	// Tanks loaded into each truck at the depot.
	FP::int32 CargoOxygenTanks = 14;
	FP::int32 CargoWaterTanks = 6;
	// Score is kilograms delivered plus this per second a crew arrives ahead of par.
	float ParTimeSeconds = 180;
	float TimeBonusPerSecond = 1;
	// Seconds between life support readouts in the log, until there is a HUD. Tab prints one any time.
	float StatusLogSeconds = 5;
};

enum class MatchPhase : FP::uint8
{
	Lobby,
	Countdown,
	Racing,
	Results,
};

struct CrewResult
{
	bool Arrived = false;
	float ArrivalTime = 0;
	float OxygenDeliveredKg = 0;
	float WaterDeliveredKg = 0;
	float Score = 0;
};

// Added to the Match entity when play starts. Every peer runs its own copy and reaches the same
// phases from what it sees, since only transforms are shared over the network for now.
struct MatchStateComponent
{
	MatchPhase Phase = MatchPhase::Lobby;
	// Seconds left in the countdown.
	float Countdown = 0;
	float RaceTime = 0;
	// -1 when nobody won, such as when the colony ran out of oxygen first.
	FP::int32 WinnerCrew = -1;
	CrewResult Crews[CrewCount];
};

enum class CrewRole : FP::uint8
{
	Driver,
	Defender,
};

// One player. The scene holds a single networked template that each peer swaps for its own copy,
// so every player gets one of these. A Driver's entity is the truck itself, which lets the
// network share the truck's transform with no extra messages.
struct CrewMemberComponent
{
	// Set by CrewSystem while playing and never saved.
	// P1..P4 as 0..3, or -1 for a fifth player and later, who only watch.
	FP::int32 Slot = -1;
	FP::int32 Crew = -1;
	CrewRole Role = CrewRole::Driver;
	FP::Vec3 Velocity;
};

// The start line (A). Trucks line up beside it facing the colony.
struct DepotComponent
{
	// Side by side distance between the two trucks.
	float StartSpacing = 12;
};

// The destination (B).
struct ColonyComponent
{
	// A truck this close has arrived.
	float ArrivalRadius = 25;
};

// A room inside a crew's truck: its cargo bay or its cab. Placed far from the race, at floor level in
// its middle, so it can be bigger on the inside than the truck it belongs to.
struct InteriorComponent
{
	FP::int32 Crew = 0;
	FP::String Kind = "Bay"; // Bay or Cab
	// Inside the walls, from the middle. Creatures come into the bay through its walls.
	float HalfWidth = 10;
	float HalfLength = 20;
};

inline const char* GetCrewName(FP::int32 crew) { return crew == 0 ? "A" : crew == 1 ? "B" : "-"; }
inline const char* GetRoleName(CrewRole role) { return role == CrewRole::Driver ? "Driver" : "Defender"; }
