#pragma once

#include "Crew/CrewComponents.h"

#include <unordered_map>
#include <vector>

// Joins the local player's cab and cargo bay through the hatch, an open doorway in each room's back
// wall, as if the two rooms were back to back. Looking through either doorway shows the other room:
// a ghost copy of it is laid out behind the doorway every frame. Walking through moves the player to
// the real room at the same spot and heading, so the crossing has no seam. R at the cab's hatch vents
// the bay through its airlock. Where a player stands is their role: at the wheel they drive, in the
// bay they defend. Seats only decide crews and where each player starts.
//
// Runs right after the Driver's systems (DriverLauncherSystem adds it), so the cab has moved with the
// truck this frame before its ghost and the bay's are placed.
class TransitSystem : public FP::System
{
public:
	const char* GetName() const override { return "Transit"; }
	void OnStart(FP::World& world) override;
	void Update(FP::World& world, float deltaTime) override;

private:
	struct Ghost
	{
		FP::Entity Entity = FP::NullEntity;
		bool Seen = false;
	};

	// The cab hatch in cab space, read from the scene before the cab starts riding the truck.
	FP::Vec3 m_CabHatch;
	FP::Entity m_CabHatchEntity = FP::NullEntity;
	FP::Entity m_CabBody = FP::NullEntity;
	std::vector<FP::Entity> m_CabParts;
	std::vector<FP::Entity> m_BayParts;
	float m_BayScanIn = 0;
	std::unordered_map<FP::Entity, Ghost> m_Ghosts;
	float m_Reach = 2;
	bool m_NearHatch = false;
};
