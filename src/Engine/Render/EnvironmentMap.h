#pragma once

#include "Core/CoreMinimal.h"
#include "Engine/Math.h"

namespace FP
{
	// Linear panorama levels for reflections and nine diffuse lighting coefficients.
	struct EnvironmentMap
	{
		Array<Vec4> Pixels;
		Vec4 Diffuse[9] = {};
		int32 Width = 0;
		int32 Height = 0;
		int32 Levels = 0;

		bool Load(const char* fullPath);
		bool Build(const Array<Vec4>& pixels, int32 width, int32 height);
		Vec3 Sample(Vec3 direction, int32 level = 0) const;
		Vec3 Irradiance(Vec3 normal) const;
	};
}
