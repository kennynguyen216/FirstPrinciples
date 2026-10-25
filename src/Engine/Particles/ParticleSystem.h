#pragma once

#include "Engine/World.h"

namespace FP
{
	struct Particle
	{
		Vec3 Position; // world space, or the entity's space when the emitter is not WorldSpace
		Vec3 Velocity;
		float Age = 0;
		float Life = 1;
	};

	// The live particles of one emitter. Added by the simulation and never saved.
	struct ParticleStateComponent
	{
		Array<Particle> Particles;
		float Accumulator = 0; // part of a particle owed by Rate
		bool Started = false;
	};

	namespace Particles
	{
		// Spawns, moves and ages the particles of every emitter. Preview repeats bursts so the
		// editor keeps showing emitters that only burst.
		void Simulate(World& world, float deltaTime, bool preview = false);

		// Drops every live particle.
		void Clear(World& world);

		// Live particles across every emitter.
		int32 Count(World& world);
	}

	// Runs particles while the world plays.
	class ParticleSystem : public System
	{
	public:
		const char* GetName() const override { return "Particles"; }
		void Update(World& world, float deltaTime) override { Particles::Simulate(world, deltaTime); }
		void OnStop(World& world) override { Particles::Clear(world); }
	};
}
