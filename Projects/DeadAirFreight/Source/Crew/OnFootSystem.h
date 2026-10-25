#pragma once

#include "Crew/CrewComponents.h"

// The local player while they are on foot, first person: the Defender in the bay. WASD walks, the
// mouse looks around, Q swaps the laser between vaporize and repair, and the left button uses it at
// the middle of the screen.
//
// Vaporize hurts creatures, and a shot that hits a tank first punctures it. Repair patches a
// tank's holes while the beam is held on it.
class OnFootSystem : public FP::System
{
public:
	const char* GetName() const override { return "OnFoot"; }
	void Update(FP::World& world, float deltaTime) override;
};
