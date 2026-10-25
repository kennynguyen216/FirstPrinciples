#pragma once

#include "Crew/CrewComponents.h"

// Moves the local player between their crew's cab and cargo bay through the hatch, so either
// crewmate can fly or defend while in transit. E at the cab's hatch climbs down to the bay, E at the
// bay's hatch climbs back up, and R at the cab's hatch vents the bay through its airlock. Where a
// player stands is their role: at the wheel they drive, in the bay they defend. Seats only decide
// crews and where each player starts.
class TransitSystem : public FP::System
{
public:
	const char* GetName() const override { return "Transit"; }
	void OnStart(FP::World& world) override;
	void Update(FP::World& world, float deltaTime) override;

private:
	// The cab hatch in cab space, read from the scene before the cab starts riding the truck.
	FP::Vec3 m_CabHatch;
	bool m_HasCabHatch = false;
	float m_Reach = 2;
	bool m_NearHatch = false;
};
