#pragma once

#include "LifeSupport.h"
#include "Engine/Math.h"

#include <vector>

// Scratch tickets bought with cargo oxygen. The coating is a grid of cells the coin really clears;
// once enough is gone the prize shows and applies. Prizes are a table so more can be added later.

enum class Prize
{
	Dud,
	CalmHands,
	FreeFix,
	Jackpot,
	Count,
};

const char* GetPrizeName(Prize prize);

// Odds for each prize, from settings, in Prize order.
std::vector<float> GetPrizeOdds(const DriverSettings& settings);

// Picks a prize from the odds with roll in 0..1.
Prize RollPrize(float roll, const std::vector<float>& odds);

struct Ticket
{
	bool Active = false;
	int Columns = 0, Rows = 0;
	std::vector<unsigned char> Coating; // 1 still covered, row by row
	FP::Vec2 Coin;                       // in cells, 0..Columns and 0..Rows
	Prize Won = Prize::Dud;
	bool Revealed = false;
	float CloseTimer = 0; // seconds the revealed prize stays up
};

// Buys a ticket if there's cargo to pay for it. False if the Driver can't afford one.
bool BuyTicket(Ticket& ticket, LifeSupport& life, Prize prize, const DriverSettings& settings);

// Clears coating cells within the coin's radius of its position.
void ScratchAt(Ticket& ticket, float radiusCells);

float GetScratchedFraction(const Ticket& ticket);

// Applies the prize to life support (the Driver's side only).
void ApplyPrize(Prize prize, LifeSupport& life, const DriverSettings& settings);

// One frame of a ticket: move the coin by mouse movement in pixels, scratch while held, reveal and
// apply the prize past the reveal fraction, close after a moment. True on the frame the prize applies.
bool StepTicket(Ticket& ticket, LifeSupport& life, FP::Vec2 mouseMove, bool scratching, const DriverSettings& settings, float deltaTime);
