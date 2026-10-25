#pragma once

#include "Engine/Engine.h"

// What the laser does to the bay. The host applies these, for its own player and for players on
// other machines, whose OnFootSystem sends them over instead.
namespace BayActions
{
	// A vaporize hit on a creature. Kills it when its health runs out.
	void HitSiphon(FP::World& world, FP::Entity siphon, float damage);
	// A vaporize shot that hit a tank first.
	void PunctureTank(FP::World& world, FP::Entity tank, FP::Vec3 where);
	// Repair beam time on a tank. Patches a hole each PatchSecondsPerHole.
	void RepairTank(FP::World& world, FP::Entity tank, float seconds, FP::Vec3 where);

	// A crew's tank or creature by its index, or NullEntity.
	FP::Entity FindTank(FP::World& world, FP::int32 crew, FP::int32 index);
	FP::Entity FindSiphon(FP::World& world, FP::int32 crew, FP::int32 index);
}
