#include "Engine/Random.h"

#include <cmath>
#include <random>

namespace FP::Random
{
	namespace
	{
		std::mt19937& Engine()
		{
			static thread_local std::mt19937 engine{ std::random_device{}() };
			return engine;
		}
	}

	float Range(float min, float max)
	{
		return min + (max - min) * Value();
	}

	int32 Range(int32 min, int32 max)
	{
		if (max < min)
			return min;
		return std::uniform_int_distribution<int32>(min, max)(Engine());
	}

	float Value()
	{
		return std::uniform_real_distribution<float>(0.0f, 1.0f)(Engine());
	}

	bool Chance(float probability)
	{
		return Value() < probability;
	}

	Vec3 Direction()
	{
		// Uniform on the sphere from a random height and angle.
		const float z = Range(-1.0f, 1.0f);
		const float angle = Range(0.0f, 2 * Pi);
		const float r = std::sqrt(1 - z * z);
		return { r * std::cos(angle), r * std::sin(angle), z };
	}

	void Seed(uint32 seed)
	{
		Engine().seed(seed);
	}
}
