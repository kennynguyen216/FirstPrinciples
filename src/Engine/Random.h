#pragma once

#include "Engine/Math.h"

// Random numbers for gameplay. Not for anything secret, use the OS for that.
namespace FP::Random
{
	// A float from min up to max.
	float Range(float min, float max);

	// An int from min to max, both included.
	int32 Range(int32 min, int32 max);

	// 0 up to 1.
	float Value();

	// True with the given chance from 0 to 1.
	bool Chance(float probability);

	// A direction of length 1.
	Vec3 Direction();

	// The same seed gives the same numbers after it, for repeatable runs.
	void Seed(uint32 seed);
}
