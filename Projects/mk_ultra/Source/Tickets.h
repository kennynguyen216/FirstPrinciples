#pragma once

#include "LifeSupport.h"
#include "Engine/Math.h"

#include <vector>

enum class Prize
{
	Dud,
	CalmHands,
	FreeFix,
	Jackpot,
	Count,
};

const char* GetPrizeName(Prize prize);

std::vector<float> GetPrizeOdds(const DriverSettings& settings);

Prize RollPrize(float roll, const std::vector<float>& odds);

struct Ticket
{
	bool Active = false;
	int Columns = 0, Rows = 0;
	std::vector<unsigned char> Coating;
	FP::Vec2 Coin;
	Prize Won = Prize::Dud;
	bool Revealed = false;
	float CloseTimer = 0;
};

bool BuyTicket(Ticket& ticket, LifeSupport& life, Prize prize, const DriverSettings& settings);

void ScratchAt(Ticket& ticket, float radiusCells);

float GetScratchedFraction(const Ticket& ticket);

void ApplyPrize(Prize prize, LifeSupport& life, const DriverSettings& settings);

bool StepTicket(Ticket& ticket, LifeSupport& life, FP::Vec2 mouseMove, bool scratching, const DriverSettings& settings, float deltaTime);
