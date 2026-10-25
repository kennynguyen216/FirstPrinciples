#pragma once

#include "Enemies/SiphonComponents.h"

// Calls other systems make on the creature pool.
namespace Siphons
{
	// Wakes pooled creatures in a crew's bay. Index picks the spot on the bay's walls, so passing the
	// same index for both crews puts their creatures in the same places. Returns how many woke,
	// which is fewer than count when the pool runs dry.
	FP::int32 Spawn(FP::World& world, FP::int32 crew, SiphonKind kind, FP::int32 count, FP::uint32 index);

	// Kills a creature: what it ate goes back into the crew's tanks and it returns to the pool.
	void Kill(FP::World& world, FP::Entity entity);

	// Blows a creature out of the airlock: what it ate is lost with it and it returns to the pool.
	void Flush(FP::World& world, FP::Entity entity);

	FP::int32 CountActive(FP::World& world, FP::int32 crew);
}

// Fills each crew's pool when play starts, then lets a Mite into both bays every few seconds.
// Hazards (M6) add surges through Siphons::Spawn.
class SiphonSpawnerSystem : public FP::System
{
public:
	const char* GetName() const override { return "SiphonSpawner"; }
	void OnStart(FP::World& world) override;
	void Update(FP::World& world, float deltaTime) override;

private:
	float m_SinceTrickle = 0;
	FP::uint32 m_NextIndex = 0;
};

// Mites fly to the nearest oxygen tank in their bay, latch on and drain it into their bellies.
// One that stays latched long enough chews a hole.
class SiphonSystem : public FP::System
{
public:
	const char* GetName() const override { return "Siphon"; }
	void Update(FP::World& world, float deltaTime) override;
};
