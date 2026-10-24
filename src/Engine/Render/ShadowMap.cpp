#include "Engine/Render/ShadowMap.h"

#include <cmath>

namespace FP
{
	namespace
	{
		Quat Inverse(const Quat& q) { return { -q.X, -q.Y, -q.Z, q.W }; }
	}

	bool ShadowView::Touches(Vec3 center, float radius) const
	{
		const Vec3 local = Inverse(LightRotation).Rotate(center - Center);
		// The light looks down -Z, so +Z is toward it.
		return std::fabs(local.X) <= Radius + radius && std::fabs(local.Y) <= Radius + radius
			&& local.Z >= -Radius - radius && local.Z <= Radius + CasterReach + radius;
	}

	ShadowView FitShadowView(const RenderView& view, const Quat& lightRotation, float distance, int32 resolution)
	{
		ShadowView out;
		out.LightRotation = lightRotation;
		out.Radius = std::fmax(distance, 0.01f) * 0.5f;
		const float texel = out.Radius * 2 / static_cast<float>(resolution > 0 ? resolution : 1);

		// Snap in light space, across the light only, so moving the camera shifts the map by whole texels.
		Vec3 center = Inverse(lightRotation).Rotate(view.CameraPosition + view.CameraForward * out.Radius);
		center.X = std::floor(center.X / texel) * texel;
		center.Y = std::floor(center.Y / texel) * texel;
		out.Center = lightRotation.Rotate(center);

		const Mat4 lightView = Mat4::TRS(out.Center, lightRotation, { 1, 1, 1 }).Inverse();
		const Mat4 projection = Mat4::Orthographic(-out.Radius, out.Radius, -out.Radius, out.Radius, -out.Radius - ShadowView::CasterReach, out.Radius);
		out.ViewProjection = projection * lightView;

		// Clip space to texture space. Texture v runs down while clip y runs up.
		Mat4 bias;
		bias.At(0, 0) = 0.5f;
		bias.At(1, 1) = -0.5f;
		bias.At(3, 0) = 0.5f;
		bias.At(3, 1) = 0.5f;
		out.ToTexture = bias * out.ViewProjection;
		return out;
	}
}
