#pragma once

#include "Engine/World.h"

namespace FP
{
	// Walks every entity with a PlayerInput and a CharacterController from the keyboard and
	// mouse. Add it before PhysicsSystem so the step uses this frame's input.
	class PlayerInputSystem : public System
	{
	public:
		const char* GetName() const override { return "Player Input"; }
		void Update(World& world, float deltaTime) override;
	};
}
