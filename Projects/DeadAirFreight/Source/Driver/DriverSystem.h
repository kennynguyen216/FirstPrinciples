#pragma once

#include "Asteroids.h"
#include "Cab.h"
#include "LifeSupport.h"
#include "SpaceWeather.h"
#include "Tickets.h"
#include "Truck.h"

#include "Engine/Engine.h"

#include <string>
#include <vector>

// How this machine takes part in a networked match (Dead Air Freight's DriverNet). The host
// simulates the truck and its life support. Everyone else shows the host's truck, which arrives
// each frame, and only runs their own player: walking, sitting and their camera.
struct DriverNetSetup
{
	bool Authority = true;
	// The host's match package, so everyone flies through the same weather and asteroids.
	bool HasPackage = false;
	MatchPackage Package;
};

class DriverSystem : public FP::System
{
public:
	DriverSystem() = default;
	explicit DriverSystem(DriverNetSetup net) : m_Net(std::move(net)) {}

	const char* GetName() const override { return "Driver"; }
	void OnStart(FP::World& world) override;
	void Update(FP::World& world, float deltaTime) override;

	const MatchPackage& GetPackage() const { return m_Package; }
	// On the host: a crewmate on another machine who holds the wheel, steers or repairs the scrubber.
	void SetRemoteCrew(bool atWheel, TruckControls controls, bool repairing)
	{
		m_RemoteAtWheel = atWheel;
		m_RemoteControls = controls;
		m_RemoteRepairing = repairing;
	}
	// Someone else holds the wheel, so this player cannot sit down at it.
	void SetWheelTaken(bool taken) { m_WheelTaken = taken; }
	// What this machine's player did last frame, for sending to the host.
	bool IsLocalSeated() const { return m_LocalSeated; }
	TruckControls GetLocalControls() const { return m_LocalControls; }
	bool IsLocalRepairing() const { return m_LocalRepairing; }
	// Away from the host: the host's match ended, so show its results.
	void ShowResults(FP::World& world, const char* reason);

private:
	struct CabPart
	{
		FP::Entity Entity;
		FP::Vec3 Position;
		FP::Quat Rotation;
	};

	struct Message
	{
		std::string Text;
		FP::Color Color;
		float TimeLeft = 0;
	};

	void Simulate(TruckState& truck, LifeSupport& life, Ticket& ticket, DriverState* driver, float deltaTime);
	void Finish(const char* reason, const TruckState& truck, const LifeSupport& life);
	void Notify(const std::string& text, FP::Color color, float seconds);

	FP::Quat GetTruckRotation(const TruckState& truck) const;
	float GetAltitude(const TruckState& truck) const;
	float GetSpeedRatio(const TruckState& truck) const;
	FP::Vec3 CabToWorld(const TruckState& truck, FP::Vec3 cabPoint) const;
	void Display(FP::World& world, const TruckState& truck, const LifeSupport& life, const Ticket& ticket, const DriverState* driver, float deltaTime);
	void DrawHud(FP::World& world, const TruckState& truck, const LifeSupport& life, const Ticket& ticket, const DriverState* driver);

	DriverSettings m_Settings;
	MatchPackage m_Package;
	Route m_Route;
	FP::Vec3 m_Colony;
	std::vector<Rock> m_Rocks;
	std::vector<FP::Entity> m_RockEntities;

	FP::Entity m_Truck = FP::NullEntity;
	FP::Entity m_Driver = FP::NullEntity;
	FP::Entity m_TravelMarker = FP::NullEntity;
	FP::Entity m_CabLight = FP::NullEntity;
	float m_CabLightIntensity = 0;
	FP::Vec3 m_Wheel;
	FP::Vec3 m_Scrubber;
	FP::Vec3 m_CanisterRack;
	// The scrubber's status light, red while it is broken.
	FP::Entity m_ScrubberLight = FP::NullEntity;
	// Canister1, Canister2...: the spares standing on the rack, or the one in hand.
	std::vector<CabPart> m_Canisters;
	CabBounds m_CabBounds;
	FP::Vec3 m_CabOrigin;
	FP::Vec3 m_SeatEye;
	std::vector<CabPart> m_CabParts;
	float m_Bank = 0;
	float m_BaseFieldOfView = 60;

	bool m_CursorFree = false;
	float m_WindNow = 0;
	float m_KpNow = 0;
	float m_PeakWindFought = 0;
	std::vector<Message> m_Messages;
	bool m_Finished = false;
	std::vector<std::string> m_Results;
	FP::Entity m_HudCamera = FP::NullEntity;

	DriverNetSetup m_Net;
	bool m_RemoteAtWheel = false;
	TruckControls m_RemoteControls;
	bool m_RemoteRepairing = false;
	bool m_WheelTaken = false;
	bool m_LocalSeated = false;
	TruckControls m_LocalControls;
	bool m_LocalRepairing = false;
	float m_LastYaw = 0;
};
