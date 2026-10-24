#pragma once

#include "DriverSettings.h"

// The cab as a tiny habitat: oxygen, CO2, the air scrubber, power, and the colony's countdown.
// Oxygen use follows the real-ratio rule: real kg per person per day, sped up by the same time
// compression that squeezes the weather window into the match.

struct LifeSupport
{
	float CabReserve = 0;  // kg O2 the cab breathes first
	float Cargo = 0;       // kg O2 being hauled to the colony
	float Co2 = 0;         // 0 safe .. 1 blackout
	float Power = 1;       // 0..1, dips during a PowerFlicker
	float PowerFlicker = 0; // seconds of dip left
	float Blackout = 0;     // seconds of blackout left
	float RepairProgress = 0; // seconds E has been held at the scrubber
	float CalmHands = 0;    // seconds of Calm Hands left
	float ColonyTimeLeft = 0; // seconds until the colony runs out
	float TicketSpend = 0;  // kg of cargo spent on tickets
	int Tickets = 0;
	bool ScrubberBroken = false;
	bool AutoFix = false;   // Free Fix banked for the next break
};

// Game seconds per real second: WeatherWindowHours squeezed into MatchLength.
float GetTimeCompression(const DriverSettings& settings);

// kg O2 per game second the crew breathes.
float GetBreathingRate(const DriverSettings& settings);

float GetTicketCost(const DriverSettings& settings); // kg of cargo

LifeSupport StartLifeSupport(const DriverSettings& settings);

// One frame: breathing (cab reserve, then cargo), CO2, blackout, power, timers and the colony clock.
// repairing is true while E is held at the scrubber.
void StepLifeSupport(LifeSupport& life, bool repairing, const DriverSettings& settings, float deltaTime);

// Breaks the scrubber (nothing calls this right now; flares used to). A banked Free Fix repairs it on the spot.
void BreakScrubber(LifeSupport& life);

// Wobble and jerk multiplier: stronger as CO2 makes the Driver woozy, much weaker under Calm Hands.
float GetDisturbanceScale(const LifeSupport& life, const DriverSettings& settings);

// The Driver can't act while blacked out.
inline bool IsBlackedOut(const LifeSupport& life) { return life.Blackout > 0; }
