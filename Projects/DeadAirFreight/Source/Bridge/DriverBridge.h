#pragma once

#include "Engine/Engine.h"

// Joins the Driver's game (Source/Driver, as its author wrote it apart from DriverState::InCab for
// the hatch to the cargo bay) to the rest of Dead Air Freight:
//
// - At GO every seated local player gets the Driver's systems: the cab, the truck and its HUD.
//   Either crewmate can then fly or defend (see TransitSystem).
// - The Driver's single cargo number is the crew's oxygen tanks. Each frame it is set to what the
//   tanks hold before the Driver's systems run, and whatever they spent (breathing, tickets) or won
//   back (jackpots) goes into the tanks after. So Mites, leaks and missed shots in the bay show on
//   the Driver's HUD, and the Driver's spending empties the tanks the Defender guards.
// - The Driver's cab breathes for the crew, so LifeSupportSystem leaves that crew's cab alone.
// - When the Driver's match ends (delivered, the colony out of air, or no cargo left) the shared
//   match goes to Results too.
// The scene has one truck, and it is Crew A's. A second truck for Crew B needs the Driver's
// systems to run more than one.
inline constexpr FP::int32 TruckCrew = 0;

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

// Runs just after the Driver's systems: their spending comes out of the tanks, crewmates in the cab
// are placed in it, the host sends the truck, and the end of the Driver's match ends the shared one.
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
	// The cab floor and seat in cab space, read before the cab starts riding the truck.
	FP::Vec3 m_CabOrigin;
	FP::Vec3 m_Seat;
};
