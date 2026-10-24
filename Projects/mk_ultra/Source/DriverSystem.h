#pragma once

#include "Asteroids.h"
#include "Cab.h"
#include "Hud.h"
#include "LifeSupport.h"
#include "SpaceWeather.h"
#include "Tickets.h"
#include "Truck.h"

#include "Engine/Engine.h"

#include <string>
#include <vector>

// Runs the Driver in scenes that have an entity named "Truck". Scenes without one are left alone.
//
// Match start: gathers real space weather into a Match Package, places the colony and the asteroid
// fields. Every frame: input, flight, collisions, life support and tickets, then display.
// All truck state lives on the Truck entity as TruckState, LifeSupport and Ticket components, so
// networking can sync that one entity.
//
// With a "Driver" entity the Driver starts on foot in the cab (floor "CabFloor"): E at the "Wheel"
// sits and stands, holding E at the "Scrubber" repairs it. Without one, the Driver is always seated
// and a chase camera follows the truck. The cab (entities named "Cab...", "Wheel" and "Scrubber") is
// laid out anywhere in the scene and rides inside the truck: the game keeps Driver logic in that
// layout and moves the parts with the truck every frame.
class DriverSystem : public FP::System
{
public:
	const char* GetName() const override { return "Driver"; }
	void OnStart(FP::World& world) override;
	void Update(FP::World& world, float deltaTime) override;

private:
	// A cab entity's layout as placed in the scene, relative to the cab floor.
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
	FP::Vec3 CabToWorld(const TruckState& truck, FP::Vec3 cabPoint) const;
	void Display(FP::World& world, const TruckState& truck, const LifeSupport& life, const Ticket& ticket, const DriverState* driver);
	void DrawHud(FP::World& world, const TruckState& truck, const LifeSupport& life, const Ticket& ticket, const DriverState* driver);
	void DrawTicket(FP::World& world, const Ticket& ticket);
	void DrawResults(FP::World& world);

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
	CabBounds m_CabBounds;
	FP::Vec3 m_CabOrigin;
	FP::Vec3 m_SeatEye;
	std::vector<CabPart> m_CabParts;
	float m_Bank = 0;

	FP::Vec2 m_LastMouse;
	float m_WindNow = 0;
	float m_KpNow = 0;
	float m_PeakWindFought = 0;
	std::vector<Message> m_Messages;
	bool m_Finished = false;
	std::vector<std::string> m_Results;
	Hud m_Hud;
};
