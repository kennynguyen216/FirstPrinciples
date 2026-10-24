#pragma once

#include "Bay/BayComponents.h"

// The local Defender, first person in their crew's cargo bay. WASD walks, holding the right mouse
// button looks around, the left button fires the laser at the middle of the screen. A shot that
// hits a creature hurts it. One that hits a tank first punctures it.
class DefenderSystem : public FP::System
{
public:
	const char* GetName() const override { return "Defender"; }
	void Update(FP::World& world, float deltaTime) override;
};
