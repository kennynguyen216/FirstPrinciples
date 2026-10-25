#pragma once

#include "Engine/World.h"

#include <cmath>

namespace FP
{
	// Gravity of Schwarzschild black holes, the Schwarzschild geodesic equation in Newtonian form:
	//     a = -GM r / |r|^3 * (1 + 3 h^2 / (c^2 |r|^2)),   h = |r x v|,   GM = Rs c^2 / 2
	// It reproduces general relativity's orbits exactly (in proper time): perihelion precession,
	// the photon sphere at 1.5 Rs, the innermost stable orbit at 3 Rs and capture past it.
	namespace BlackHole
	{
		struct Field
		{
			Vec3 Position;
			Vec3 Velocity;
			float SchwarzschildRadius;
			float SpeedOfLight;
		};

		inline Vec3 Acceleration(Vec3 position, Vec3 velocity, const Field* holes, int32 count)
		{
			Vec3 total;
			for (int32 i = 0; i < count; ++i)
			{
				const Field& hole = holes[i];
				const Vec3 d = position - hole.Position;
				const float r2 = d.LengthSquared();
				if (r2 < 1e-8f)
					continue;
				const float c2 = hole.SpeedOfLight * hole.SpeedOfLight;
				const float gm = 0.5f * hole.SchwarzschildRadius * c2;
				const float h2 = Vec3::Cross(d, velocity - hole.Velocity).LengthSquared();
				total -= d * (gm / (r2 * std::sqrt(r2)) * (1 + 3 * h2 / (c2 * r2)));
			}
			return total;
		}

		// Moves a body for deltaTime with RK4, splitting into smaller steps near the holes.
		// Returns the index of the hole it fell into, or -1. Holes move linearly during the step.
		inline int32 Step(Vec3& position, Vec3& velocity, Vec3 thrust, Field* holes, int32 count, float deltaTime)
		{
			// Step at most about 1% of an orbit at the closest hole so passes near the horizon stay accurate.
			float maxStep = deltaTime;
			for (int32 i = 0; i < count; ++i)
			{
				const float r = std::fmax((position - holes[i].Position).Length(), holes[i].SchwarzschildRadius);
				const float orbitSpeed = std::sqrt(0.5f * holes[i].SchwarzschildRadius / r) * holes[i].SpeedOfLight;
				maxStep = std::fmin(maxStep, 0.06f * r / (orbitSpeed + (velocity - holes[i].Velocity).Length() + 1e-4f));
			}
			// ponytail: capped substeps, a body skimming the horizon at huge speed can tunnel. Raise if it matters
			const int32 steps = static_cast<int32>(std::fmin(std::ceil(deltaTime / std::fmax(maxStep, 1e-6f)), 1024.0f));
			const float dt = deltaTime / static_cast<float>(steps);

			auto accel = [&](Vec3 p, Vec3 v) { return Acceleration(p, v, holes, count) + thrust; };
			for (int32 s = 0; s < steps; ++s)
			{
				const Vec3 a1 = accel(position, velocity);
				const Vec3 v2 = velocity + a1 * (dt * 0.5f);
				const Vec3 a2 = accel(position + velocity * (dt * 0.5f), v2);
				const Vec3 v3 = velocity + a2 * (dt * 0.5f);
				const Vec3 a3 = accel(position + v2 * (dt * 0.5f), v3);
				const Vec3 v4 = velocity + a3 * dt;
				const Vec3 a4 = accel(position + v3 * dt, v4);
				position += (velocity + (v2 + v3) * 2 + v4) * (dt / 6);
				velocity += (a1 + (a2 + a3) * 2 + a4) * (dt / 6);

				for (int32 i = 0; i < count; ++i)
				{
					holes[i].Position += holes[i].Velocity * dt;
					if ((position - holes[i].Position).Length() <= holes[i].SchwarzschildRadius)
						return i;
				}
			}
			return -1;
		}

		// Speed for a circular orbit at radius r. Infinite at the photon sphere (1.5 Rs).
		inline float CircularSpeed(float r, float schwarzschildRadius, float speedOfLight)
		{
			return speedOfLight * std::sqrt(0.5f * schwarzschildRadius / (r - 1.5f * schwarzschildRadius));
		}
	}

	// Pulls every GravityBody toward every BlackHole. A moving hole hands out real gravity assists.
	// Add it before PhysicsSystem so the step uses this frame's velocities.
	class BlackHoleSystem : public System
	{
	public:
		const char* GetName() const override { return "Black Hole"; }
		void OnStart(World& world) override;
		void Update(World& world, float deltaTime) override;
	};
}
