#pragma once

#include "Engine/Engine.h"

// What the laser does to the bay.
namespace BayActions
{
	// A vaporize hit on a creature. Kills it when its health runs out.
	void HitSiphon(FP::World& world, FP::Entity siphon, float damage);
	// A vaporize shot that hit a tank first.
	void PunctureTank(FP::World& world, FP::Entity tank, FP::Vec3 where);
	// Repair beam time on a tank. Patches a hole each PatchSecondsPerHole.
	void RepairTank(FP::World& world, FP::Entity tank, float seconds, FP::Vec3 where);
}
