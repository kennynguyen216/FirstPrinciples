#pragma once

#include "Engine/Engine.h"

// Joins the Driver's game (Source/Driver, kept exactly as its author wrote it) to the rest of
// Dead Air Freight without changing it:
//
// - At GO, a seated local Driver gets the Driver's systems, so the lobby, seats and countdown still
//   decide who drives.
// - The Driver's single cargo number is the crew's oxygen tanks. Each frame it is set to what the
//   tanks hold before the Driver's systems run, and whatever they spent (breathing, tickets) or won
//   back (jackpots) goes into the tanks after. So Mites, leaks and missed shots in the bay show on
//   the Driver's HUD, and the Driver's spending empties the tanks the Defender guards.
// - The Driver's cab breathes for the crew, so LifeSupportSystem leaves that crew's cab alone.
// - The Driver's truck is copied onto the Driver's networked crew entity, ready for multiplayer.
// - When the Driver's match ends (delivered, the colony out of air, or no cargo left) the shared
//   match goes to Results too.
class DriverLauncherSystem : public FP::System
{
public:
	const char* GetName() const override { return "DriverLauncher"; }
	void OnStart(FP::World& world) override;
	void Update(FP::World& world, float deltaTime) override;

private:
	bool m_Launched = false;
};

// Runs just before the Driver's systems: the Driver's cargo becomes what the tanks hold.
class CargoBridgeInSystem : public FP::System
{
public:
	explicit CargoBridgeInSystem(FP::int32 crew) : m_Crew(crew) {}
	const char* GetName() const override { return "CargoBridgeIn"; }
	void Update(FP::World& world, float deltaTime) override;

private:
	FP::int32 m_Crew;
};

// Runs just after the Driver's systems: their spending comes out of the tanks, the truck is copied
// to the crew entity, and the end of the Driver's match ends the shared one.
class CargoBridgeOutSystem : public FP::System
{
public:
	explicit CargoBridgeOutSystem(FP::int32 crew) : m_Crew(crew) {}
	const char* GetName() const override { return "CargoBridgeOut"; }
	void OnStart(FP::World& world) override;
	void Update(FP::World& world, float deltaTime) override;

private:
	FP::int32 m_Crew;
	float m_DeliveryAltitude = 0;
	float m_ColonyRadius = 0;
};
