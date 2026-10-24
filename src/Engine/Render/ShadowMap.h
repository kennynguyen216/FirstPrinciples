#pragma once

#include "Engine/Render/RenderView.h"

namespace FP
{
	// Where a directional light's shadow map sits for one frame. An orthographic box lined up
	// with the light, Radius on each side of Center across the light and deeper toward it.
	struct ShadowView
	{
		Mat4 ViewProjection; // world to shadow clip space, depth 0 to 1
		Mat4 ToTexture;      // world to shadow map UV in xy and depth in z
		Vec3 Center;
		float Radius = 0;
		Quat LightRotation;

		// How far toward the light casters are still caught, so tall things outside the box
		// still shade what is inside.
		static constexpr float CasterReach = 50;

		// True when a sphere could cast into the box. Used to skip casters.
		bool Touches(Vec3 center, float radius) const;
	};

	// Fits a box distance wide just ahead of the camera. The center snaps to whole shadow texels so
	// shadow edges stay put as the camera moves instead of crawling.
	ShadowView FitShadowView(const RenderView& view, const Quat& lightRotation, float distance, int32 resolution);
}
