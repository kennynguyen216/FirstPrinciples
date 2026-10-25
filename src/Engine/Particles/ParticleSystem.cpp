#include "Engine/Particles/ParticleSystem.h"
#include "Engine/Random.h"

#include <algorithm>
#include <cmath>

namespace FP
{
	namespace
	{
		constexpr float GravityPull = 9.81f;

		// A random direction within spread degrees of around.
		Vec3 InCone(Vec3 around, float spread)
		{
			const Vec3 axis = around.Length() > 1e-6f ? around.Normalized() : Vec3(0, 1, 0);
			const float cosSpread = std::cos(Clamp(spread, 0.0f, 180.0f) * Pi / 180.0f);
			const float z = Random::Range(cosSpread, 1.0f);
			const float ring = std::sqrt(std::max(0.0f, 1 - z * z));
			const float angle = Random::Range(0.0f, 2 * Pi);
			// Any two directions at right angles to the axis.
			const Vec3 helper = std::fabs(axis.Y) < 0.99f ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
			const Vec3 side = Vec3::Cross(axis, helper).Normalized();
			const Vec3 other = Vec3::Cross(axis, side);
			return axis * z + side * (ring * std::cos(angle)) + other * (ring * std::sin(angle));
		}

		Vec3 InShape(const ParticleEmitterComponent& emitter)
		{
			if (emitter.Shape == "Sphere")
				return Random::Direction() * (emitter.ShapeSize.X * std::cbrt(Random::Value()));
			if (emitter.Shape == "Box")
			{
				const Vec3 half = emitter.ShapeSize * 0.5f;
				return { Random::Range(-half.X, half.X), Random::Range(-half.Y, half.Y), Random::Range(-half.Z, half.Z) };
			}
			return {};
		}

		void Spawn(const ParticleEmitterComponent& emitter, ParticleStateComponent& state, const Mat4& world, const Quat& rotation, int32 count)
		{
			const int32 room = std::max(emitter.MaxParticles, 0) - state.Particles.Num();
			count = std::min(count, room);
			for (int32 i = 0; i < count; ++i)
			{
				Particle particle;
				particle.Position = InShape(emitter);
				const float speed = emitter.Speed * (1 - Random::Range(0.0f, Clamp01(emitter.SpeedRandom)));
				particle.Velocity = InCone(emitter.Direction, emitter.Spread) * speed;
				if (emitter.WorldSpace)
				{
					particle.Position = world.TransformPoint(particle.Position);
					particle.Velocity = rotation.Rotate(particle.Velocity);
				}
				particle.Life = std::max(emitter.Lifetime * (1 - Random::Range(0.0f, Clamp01(emitter.LifetimeRandom))), 0.01f);
				state.Particles.Add(particle);
			}
		}
	}

	namespace Particles
	{
		void Simulate(World& world, float deltaTime, bool preview)
		{
			Array<Entity> finished;
			for (auto [entity, emitter] : world.View<ParticleEmitterComponent>().each())
			{
				ParticleStateComponent* state = world.TryGet<ParticleStateComponent>(entity);
				if (!state)
					state = &world.Add<ParticleStateComponent>(entity);

				// Age and move what is already alive.
				const Vec3 pull{ 0, -GravityPull * emitter.Gravity, 0 };
				const float slow = std::max(0.0f, 1 - emitter.Drag * deltaTime);
				for (int32 i = state->Particles.Num() - 1; i >= 0; --i)
				{
					Particle& particle = state->Particles[i];
					particle.Age += deltaTime;
					if (particle.Age >= particle.Life)
					{
						state->Particles.RemoveAtSwap(i);
						continue;
					}
					particle.Velocity = (particle.Velocity + pull * deltaTime) * slow;
					particle.Position += particle.Velocity * deltaTime;
				}

				const Mat4 placed = world.GetWorldMatrix(entity);
				const Quat rotation = world.GetWorldRotation(entity);
				int32 count = emitter.PendingBurst;
				emitter.PendingBurst = 0;
				if (!state->Started)
				{
					state->Started = true;
					count += emitter.Burst;
				}
				// Previews burst again once the last burst is gone.
				else if (preview && emitter.Burst > 0 && state->Particles.IsEmpty())
				{
					count += emitter.Burst;
				}
				const bool streaming = emitter.Emitting && emitter.Rate > 0;
				if (streaming)
				{
					state->Accumulator += emitter.Rate * deltaTime;
					const int32 owed = static_cast<int32>(state->Accumulator);
					state->Accumulator -= static_cast<float>(owed);
					count += owed;
				}
				Spawn(emitter, *state, placed, rotation, count);

				if (emitter.DestroyWhenDone && !preview && !streaming && state->Particles.IsEmpty())
					finished.Add(entity);
			}
			for (Entity entity : finished)
				world.DestroyEntity(entity);
		}

		void Clear(World& world)
		{
			world.GetRegistry().clear<ParticleStateComponent>();
		}

		int32 Count(World& world)
		{
			int32 total = 0;
			for (auto [entity, state] : world.View<ParticleStateComponent>().each())
				total += state.Particles.Num();
			return total;
		}
	}
}
