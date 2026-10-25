#include "Tickets.h"

#include <algorithm>

using namespace FP;

const char* GetPrizeName(Prize prize)
{
	switch (prize)
	{
		case Prize::CalmHands: return "CALM HANDS";
		case Prize::FreeFix: return "FREE FIX";
		case Prize::Jackpot: return "JACKPOT";
		default: return "DUD";
	}
}

std::vector<float> GetPrizeOdds(const DriverSettings& settings)
{
	return { settings.PrizeDud, settings.PrizeCalmHands, settings.PrizeFreeFix, settings.PrizeJackpot };
}

Prize RollPrize(float roll, const std::vector<float>& odds)
{
	float total = 0;
	for (float odd : odds)
		total += odd;
	float pick = roll * total;
	for (size_t i = 0; i < odds.size(); ++i)
	{
		if (pick < odds[i])
			return static_cast<Prize>(i);
		pick -= odds[i];
	}
	return Prize::Dud;
}

Prize WithPity(Prize rolled, int ticketsSinceJackpot, const DriverSettings& settings)
{
	return settings.JackpotPity > 0 && ticketsSinceJackpot + 1 >= settings.JackpotPity ? Prize::Jackpot : rolled;
}

bool BuyTicket(Ticket& ticket, LifeSupport& life, Prize prize, const DriverSettings& settings)
{
	const float cost = GetTicketCost(settings);
	if (ticket.Active || life.Cargo < cost)
		return false;
	life.Cargo -= cost;
	life.TicketSpend += cost;
	++life.Tickets;
	ticket = {};
	ticket.Active = true;
	ticket.Columns = static_cast<int>(settings.TicketColumns);
	ticket.Rows = static_cast<int>(settings.TicketRows);
	ticket.Coating.assign(static_cast<size_t>(ticket.Columns * ticket.Rows), 1);
	ticket.Coin = { ticket.Columns * 0.5f, ticket.Rows * 0.5f };
	ticket.Won = prize;
	return true;
}

void ScratchAt(Ticket& ticket, float radiusCells)
{
	for (int y = 0; y < ticket.Rows; ++y)
	{
		for (int x = 0; x < ticket.Columns; ++x)
		{
			const Vec2 center{ x + 0.5f, y + 0.5f };
			const Vec2 d = center - ticket.Coin;
			if (d.X * d.X + d.Y * d.Y <= radiusCells * radiusCells)
				ticket.Coating[static_cast<size_t>(y * ticket.Columns + x)] = 0;
		}
	}
}

float GetScratchedFraction(const Ticket& ticket)
{
	if (ticket.Coating.empty())
		return 0;
	const auto covered = std::count(ticket.Coating.begin(), ticket.Coating.end(), 1);
	return 1 - static_cast<float>(covered) / static_cast<float>(ticket.Coating.size());
}

void ApplyPrize(Prize prize, LifeSupport& life, const DriverSettings& settings)
{
	switch (prize)
	{
		case Prize::CalmHands:
			life.CalmHands = settings.CalmHandsTime;
			break;
		case Prize::FreeFix:
			if (life.ScrubberBroken)
				life.ScrubberBroken = false;
			else
				life.AutoFix = true;
			break;
		case Prize::Jackpot:
			life.Cargo += GetTicketCost(settings) * settings.JackpotTickets;
			break;
		default:
			break;
	}
}

bool StepTicket(Ticket& ticket, LifeSupport& life, Vec2 mouseMove, bool scratching, const DriverSettings& settings, float deltaTime)
{
	if (!ticket.Active)
		return false;
	if (ticket.Revealed)
	{
		ticket.CloseTimer -= deltaTime;
		ticket.Active = ticket.CloseTimer > 0;
		return false;
	}
	ticket.Coin += mouseMove * settings.TicketCoinSpeed;
	ticket.Coin.X = std::clamp(ticket.Coin.X, 0.0f, static_cast<float>(ticket.Columns));
	ticket.Coin.Y = std::clamp(ticket.Coin.Y, 0.0f, static_cast<float>(ticket.Rows));
	if (scratching)
		ScratchAt(ticket, settings.TicketCoinRadius);
	if (GetScratchedFraction(ticket) < settings.TicketRevealFraction)
		return false;
	ticket.Revealed = true;
	ticket.CloseTimer = settings.TicketShowTime;
	ApplyPrize(ticket.Won, life, settings);
	return true;
}
