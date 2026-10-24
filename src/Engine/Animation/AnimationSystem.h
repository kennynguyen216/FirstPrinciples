#pragma once

#include "Engine/World.h"

namespace FP
{
	// Plays animators on skeletal meshes and writes each one's bone matrices for the renderer.
	// Add it after ScriptSystem so parameters set by scripts show the same frame.
	class AnimationSystem : public System
	{
	public:
		const char* GetName() const override { return "Animation"; }
		void Update(World& world, float deltaTime) override;

		// Drops every animator and pose so stopped worlds draw bind poses again.
		void OnStop(World& world) override;
	};
}
