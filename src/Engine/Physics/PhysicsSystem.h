#pragma once

#include "Engine/World.h"

namespace FP
{
	// Runs physics while the world plays: makes bodies for colliders, steps at a fixed rate, moves
	// entities to where their bodies went and tells scripts about collisions. Add it after
	// ScriptSystem, so forces scripts add this frame apply this frame.
	class PhysicsSystem : public System
	{
	public:
		const char* GetName() const override { return "Physics"; }
		void OnStart(World& world) override;
		void Update(World& world, float deltaTime) override;

		// Drops every body, so playing again starts fresh.
		void OnStop(World& world) override;
	};
}
