#pragma once

#include "DriverSettings.h"

struct LifeSupport
{
	float CabReserve = 0;
	float Cargo = 0;
	float Co2 = 0;
	float Power = 1;
	float PowerFlicker = 0;
	float Blackout = 0;
	float RepairProgress = 0;
	float CalmHands = 0;
	float ColonyTimeLeft = 0;
	float TicketSpend = 0;
	int Tickets = 0;
	// Spare LiOH canisters left on the rack. Slotting one into the broken scrubber fixes it.
	int Canisters = 0;
	bool ScrubberBroken = false;
	bool AutoFix = false;
};

float GetTimeCompression(const DriverSettings& settings);

float GetBreathingRate(const DriverSettings& settings);

float GetTicketCost(const DriverSettings& settings);

LifeSupport StartLifeSupport(const DriverSettings& settings);

void StepLifeSupport(LifeSupport& life, bool repairing, const DriverSettings& settings, float deltaTime);

void BreakScrubber(LifeSupport& life);

float GetDisturbanceScale(const LifeSupport& life, const DriverSettings& settings);

inline bool IsBlackedOut(const LifeSupport& life) { return life.Blackout > 0; }
