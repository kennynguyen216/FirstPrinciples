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

class DriverSystem : public FP::System
{
public:
	const char* GetName() const override { return "Driver"; }
	void OnStart(FP::World& world) override;
	void Update(FP::World& world, float deltaTime) override;

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
	float m_BaseFieldOfView = 60;

	FP::Vec2 m_LastMouse;
	float m_WindNow = 0;
	float m_KpNow = 0;
	float m_PeakWindFought = 0;
	std::vector<Message> m_Messages;
	bool m_Finished = false;
	std::vector<std::string> m_Results;
	Hud m_Hud;
};
