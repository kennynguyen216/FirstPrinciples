#pragma once

#include "Engine/Engine.h"

#include <vector>

struct DriverNetComponent;

// Joins the Driver's game (Source/Driver, as its author wrote it apart from DriverState::InCab for
// the hatch to the cargo bay) to the rest of Dead Air Freight: two trucks racing, each crewed by one
// or two players.
//
// - At GO every seated player gets the Driver's systems: the cab, the truck and its HUD, with the
//   scene's Truck moved into their crew's lane at the depot. A crew's first seated player, its
//   captain, runs the truck on their machine. A crewmate on another machine shows the truck the
//   captain sends, and sends back what they do at the wheel. Everyone flies through the host's
//   weather and asteroids. Either crewmate can fly or defend the cargo bay (see TransitSystem).
// - The Driver's single cargo number is the crew's oxygen tanks. Each frame it is set to what the
//   tanks hold before the Driver's systems run, and whatever they spent (breathing, tickets) or won
//   back (jackpots) goes into the tanks after, through the host, which runs both bays.
// - The Driver's cab breathes for the crew, so LifeSupportSystem leaves that crew's cab alone.
// - Every truck goes to everyone each frame. RivalTruckSystem draws the other crew's, marks where it
//   is, and captains bump their truck off it when the two touch.
// - The first truck to deliver wins. A truck out of cargo or out of time is out of the race, and
//   when every truck is out nobody wins.
namespace DriverBridge
{
	// On the host: oxygen a truck used (positive) or won back (negative), out of or into its tanks.
	void SpendCargo(FP::World& world, FP::int32 crew, float kg);
	// On the host: a truck delivered, or is out of the race.
	void FinishTruck(FP::World& world, FP::int32 crew, bool delivered, float cargoKg);
	// Where a captain says their truck is.
	void ReceivePose(FP::World& world, DriverNetComponent& link, FP::int32 crew, FP::Vec3 position, FP::Vec3 velocity, FP::Vec3 rotation);
}

class DriverLauncherSystem : public FP::System
{
public:
	const char* GetName() const override { return "DriverLauncher"; }
	void OnStart(FP::World& world) override;
	void Update(FP::World& world, float deltaTime) override;

private:
	bool m_Launched = false;
};

// Runs just before the Driver's systems. On the captain the Driver's cargo becomes what the tanks
// hold and the wheel goes to whoever sits first; on a crewmate the truck becomes the captain's.
class CargoBridgeInSystem : public FP::System
{
public:
	CargoBridgeInSystem(FP::int32 crew, bool captain) : m_Crew(crew), m_Captain(captain) {}
	const char* GetName() const override { return "CargoBridgeIn"; }
	void Update(FP::World& world, float deltaTime) override;

private:
	FP::int32 m_Crew;
	bool m_Captain;
};

// Runs just after the Driver's systems: crewmates in the cab are placed in it; on the captain the
// spending comes out of the tanks, the truck bumps off its rival, goes to everyone, and the end of
// its race goes to the host; on a crewmate what they did goes to the captain.
class CargoBridgeOutSystem : public FP::System
{
public:
	CargoBridgeOutSystem(FP::int32 crew, bool captain) : m_Crew(crew), m_Captain(captain) {}
	const char* GetName() const override { return "CargoBridgeOut"; }
	void OnStart(FP::World& world) override;
	void Update(FP::World& world, float deltaTime) override;

private:
	FP::int32 m_Crew;
	bool m_Captain;
	float m_DeliveryAltitude = 0;
	float m_ColonyRadius = 0;
	// The cab floor and seat in cab space, read before the cab starts riding the truck.
	FP::Vec3 m_CabOrigin;
	FP::Vec3 m_Seat;
	bool m_Reported = false; // this truck's finish has gone to the host
	bool m_Moved = false;
	FP::Vec3 m_LastPosition;
	bool m_Touching = false; // against the rival truck right now
	float m_Shake = 0; // how hard the last bump still shakes the view, 0 to 1
	float m_ShakeTime = 0;

	void Shake(FP::World& world, float deltaTime);
};

// The other crew's truck: drawn where its captain last said it was, carried on at the speed it was
// going so it glides between messages, and marked on this player's screen, with an arrow at the
// edge when it is out of view (Content/UI/RaceHud.html).
class RivalTruckSystem : public FP::System
{
public:
	explicit RivalTruckSystem(FP::int32 localCrew) : m_LocalCrew(localCrew) {}
	const char* GetName() const override { return "RivalTrucks"; }
	void OnStart(FP::World& world) override;
	void Update(FP::World& world, float deltaTime) override;

private:
	struct Rival
	{
		FP::int32 Crew = 0;
		FP::Entity Entity = FP::NullEntity;
		bool Shown = false;
		FP::Vec3 Scale;
	};

	void DrawMarkers(FP::World& world);

	FP::int32 m_LocalCrew;
	std::vector<Rival> m_Rivals;
	FP::Entity m_Hud = FP::NullEntity;
};
