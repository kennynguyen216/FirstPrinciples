#include "LifeSupport.h"

#include <algorithm>

float GetTimeCompression(const DriverSettings& settings)
{
	return settings.MatchLength > 0 ? settings.WeatherWindowHours * 3600 / settings.MatchLength : 1;
}

float GetBreathingRate(const DriverSettings& settings)
{
	return settings.OxygenPerPersonPerDay * settings.CrewSize / 86400 * GetTimeCompression(settings);
}

float GetTicketCost(const DriverSettings& settings)
{
	return settings.TicketCostFraction * settings.StartingCargo;
}

LifeSupport StartLifeSupport(const DriverSettings& settings)
{
	LifeSupport life;
	life.CabReserve = GetBreathingRate(settings) * settings.CabReserveTime;
	life.Cargo = settings.StartingCargo;
	const float colonyRate = settings.OxygenPerPersonPerDay * settings.ColonyCrew / 86400 * GetTimeCompression(settings);
	life.ColonyTimeLeft = colonyRate > 0 ? settings.ColonyReserveKg / colonyRate : 0;
	return life;
}

void StepLifeSupport(LifeSupport& life, bool repairing, const DriverSettings& settings, float deltaTime)
{
	float breath = GetBreathingRate(settings) * deltaTime;
	const float fromCab = std::min(breath, life.CabReserve);
	life.CabReserve -= fromCab;
	breath -= fromCab;
	life.Cargo = std::max(life.Cargo - breath, 0.0f);

	life.ColonyTimeLeft = std::max(life.ColonyTimeLeft - deltaTime, 0.0f);
	life.CalmHands = std::max(life.CalmHands - deltaTime, 0.0f);
	life.PowerFlicker = std::max(life.PowerFlicker - deltaTime, 0.0f);
	life.Power = life.PowerFlicker > 0 ? settings.PowerFlickerLevel : 1;

	if (life.ScrubberBroken && repairing && !IsBlackedOut(life))
	{
		life.RepairProgress += deltaTime;
		if (life.RepairProgress >= settings.ScrubberRepairTime)
			life.ScrubberBroken = false;
	}
	if (!life.ScrubberBroken || !repairing)
		life.RepairProgress = 0;

	if (life.ScrubberBroken)
		life.Co2 = std::min(life.Co2 + deltaTime / settings.Co2RiseTime, 1.0f);
	else
		life.Co2 = std::max(life.Co2 - deltaTime / settings.Co2FallTime, 0.0f);

	if (IsBlackedOut(life))
	{
		life.Blackout = std::max(life.Blackout - deltaTime, 0.0f);
		if (!IsBlackedOut(life))
			life.Co2 = std::min(life.Co2, settings.Co2AfterBlackout);
	}
	else if (life.Co2 >= 1)
	{
		life.Blackout = settings.BlackoutTime;
	}
}

void BreakScrubber(LifeSupport& life)
{
	if (life.AutoFix)
	{
		life.AutoFix = false;
		return;
	}
	life.ScrubberBroken = true;
}

float GetDisturbanceScale(const LifeSupport& life, const DriverSettings& settings)
{
	const float woozy = 1 + life.Co2 * settings.Co2WobbleBoost;
	return life.CalmHands > 0 ? woozy * settings.CalmHandsScale : woozy;
}
