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

	ShadowView PerspectiveShadowView(Vec3 position, const Quat& rotation, float halfAngle, float range)
	{
		ShadowView out;
		out.Center = position;
		out.Radius = range;
		out.LightRotation = rotation;
		const float farClip = std::fmax(range, 0.1f);
		const float nearClip = std::fmin(0.05f, farClip * 0.1f);
		out.ViewProjection = Mat4::Perspective(std::fmin(std::fmax(halfAngle, 1.0f), 90.0f) * 2.0f, 1.0f, nearClip, farClip)
			* Mat4::TRS(position, rotation, { 1, 1, 1 }).Inverse();
		Mat4 bias;
		bias.At(0, 0) = 0.5f;
		bias.At(1, 1) = -0.5f;
		bias.At(3, 0) = 0.5f;
		bias.At(3, 1) = 0.5f;
		out.ToTexture = bias * out.ViewProjection;
		return out;
	}

	Quat PointShadowFaceRotation(int32 face)
	{
		// Keep this face order in sync with Lit.frag.
		static constexpr Vec3 forward[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
		const Vec3 direction = forward[face];
		const Vec3 referenceUp = (face == 2 || face == 3) ? Vec3{ 0, 0, 1 } : Vec3{ 0, 1, 0 };
		const Vec3 right = Vec3::Cross(direction, referenceUp).Normalized();
		const Vec3 up = Vec3::Cross(right, direction);
		Mat4 basis;
		basis.At(0, 0) = right.X;
		basis.At(0, 1) = right.Y;
		basis.At(0, 2) = right.Z;
		basis.At(1, 0) = up.X;
		basis.At(1, 1) = up.Y;
		basis.At(1, 2) = up.Z;
		basis.At(2, 0) = -direction.X;
		basis.At(2, 1) = -direction.Y;
		basis.At(2, 2) = -direction.Z;
		return Quat::FromMatrix(basis);
	}
}
