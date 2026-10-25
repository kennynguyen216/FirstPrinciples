#include "Tests/TestFramework.h"
#include "Engine/Engine.h"
#include "Engine/Particles/ParticleSystem.h"

#include <cmath>

using namespace FP;

namespace
{
	Entity MakeEmitter(World& world, float rate, int32 burst)
	{
		const Entity entity = world.CreateEntity("Emitter");
		ParticleEmitterComponent& emitter = world.Add<ParticleEmitterComponent>(entity);
		emitter.Rate = rate;
		emitter.Burst = burst;
		emitter.LifetimeRandom = 0;
		emitter.SpeedRandom = 0;
		return entity;
	}

	const Array<Particle>& Live(World& world, Entity entity) { return world.Get<ParticleStateComponent>(entity).Particles; }
}

FP_TEST(Particles_BurstRateAndCap)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	const Entity entity = MakeEmitter(*world.Get(), 10, 5);

	// The burst comes first, then one particle per tenth of a second.
	Particles::Simulate(*world.Get(), 0.1f);
	FP_EXPECT(Live(*world.Get(), entity).Num() == 6);
	Particles::Simulate(*world.Get(), 0.5f);
	FP_EXPECT(Live(*world.Get(), entity).Num() == 11);

	// Never more than MaxParticles.
	world->Get<ParticleEmitterComponent>(entity).MaxParticles = 12;
	Particles::Simulate(*world.Get(), 0.5f);
	FP_EXPECT(Live(*world.Get(), entity).Num() == 12);

	// Turning it off stops new ones, and the rest die off.
	world->Get<ParticleEmitterComponent>(entity).Emitting = false;
	Particles::Simulate(*world.Get(), 2.0f);
	FP_EXPECT(Live(*world.Get(), entity).IsEmpty());

	// Scripts queue bursts.
	world->Get<ParticleEmitterComponent>(entity).PendingBurst = 4;
	Particles::Simulate(*world.Get(), 0.01f);
	FP_EXPECT(Live(*world.Get(), entity).Num() == 4 && Particles::Count(*world.Get()) == 4);
	Particles::Clear(*world.Get());
	FP_EXPECT(Particles::Count(*world.Get()) == 0);
}

FP_TEST(Particles_FlyAlongTheEntityAndFall)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	const Entity entity = MakeEmitter(*world.Get(), 0, 1);
	ParticleEmitterComponent& emitter = world->Get<ParticleEmitterComponent>(entity);
	emitter.Spread = 0;
	emitter.Speed = 2;
	emitter.Lifetime = 0.5f;
	world->Get<TransformComponent>(entity).Position = { 3, 0, 0 };
	// Turned so its up points along -X.
	world->Get<TransformComponent>(entity).Rotation = { 0, 0, 90 };

	Particles::Simulate(*world.Get(), 0.001f);
	const Particle first = Live(*world.Get(), entity)[0];
	FP_EXPECT(std::fabs(first.Position.X - 3) < 0.01f);
	FP_EXPECT(first.Velocity.X < -1.99f && std::fabs(first.Velocity.Y) < 0.01f);

	// Gravity pulls it down.
	world->Get<ParticleEmitterComponent>(entity).Gravity = 1;
	Particles::Simulate(*world.Get(), 0.2f);
	FP_EXPECT(Live(*world.Get(), entity)[0].Velocity.Y < -1.5f);

	// It dies at its lifetime.
	Particles::Simulate(*world.Get(), 0.4f);
	FP_EXPECT(Live(*world.Get(), entity).IsEmpty());
}

FP_TEST(Particles_FinishedEmittersGoAwayAndPreviewsLoop)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	const Entity blast = MakeEmitter(*world.Get(), 0, 10);
	world->Get<ParticleEmitterComponent>(blast).Lifetime = 0.2f;
	world->Get<ParticleEmitterComponent>(blast).DestroyWhenDone = true;

	// Previews replay a burst once it is gone, and never destroy anything.
	Particles::Simulate(*world.Get(), 0.01f, true);
	Particles::Simulate(*world.Get(), 0.3f, true);
	Particles::Simulate(*world.Get(), 0.01f, true);
	FP_EXPECT(world->IsValid(blast) && Live(*world.Get(), blast).Num() == 10);

	// Playing, it is removed once its burst has died.
	Particles::Clear(*world.Get());
	world->AddSystem<ParticleSystem>();
	world->BeginPlay();
	for (int32 i = 0; i < 30; ++i)
		world->Update(1.0f / 60.0f);
	FP_EXPECT(!world->IsValid(blast));
	world->EndPlay();
}
