#include "Engine/Render/RenderView.h"

namespace FP
{
	namespace
	{
		// Ortho cameras see this far in front of and behind them, enough for any 2D layering.
		constexpr float OrthoDepth = 1000.0f;
	}

	bool Ray::IntersectZPlane(float planeZ, Vec3& outPoint) const
	{
		if (std::fabs(Direction.Z) < 1e-6f)
			return false;
		const float t = (planeZ - Origin.Z) / Direction.Z;
		if (t < 0)
			return false;
		outPoint = Origin + Direction * t;
		return true;
	}

	RenderView RenderView::Expanded(float factor) const
	{
		RenderView result = *this;
		factor = std::isfinite(factor) ? std::clamp(factor, 1.0f, 4.0f) : 1.0f;
		// Scale both clip-space rows, including translation for off-center ortho views.
		for (int column = 0; column < 4; ++column)
		{
			result.Projection.At(column, 0) /= factor;
			result.Projection.At(column, 1) /= factor;
		}
		result.PixelsPerUnit /= factor;
		return result;
	}

	Ray RenderView::ScreenToRay(Vec2 pixel, float* farDistance) const
	{
		const float x = pixel.X / static_cast<float>(Width) * 2.0f - 1.0f;
		const float y = 1.0f - pixel.Y / static_cast<float>(Height) * 2.0f;
		const Mat4 inverse = GetViewProjection().Inverse();
		const Vec4 nearPoint = inverse * Vec4(x, y, 0, 1);
		const Vec4 farPoint = inverse * Vec4(x, y, 1, 1);
		const Vec3 a = nearPoint.XYZ() / nearPoint.W;
		const Vec3 b = farPoint.XYZ() / farPoint.W;
		if (farDistance) *farDistance = (b - a).Length();
		return { a, (b - a).Normalized() };
	}

	Vec2 RenderView::WorldToScreen(Vec3 world) const
	{
		const Vec4 clip = GetViewProjection() * Vec4(world, 1);
		const float w = std::fabs(clip.W) > 1e-6f ? clip.W : 1e-6f;
		return { (clip.X / w * 0.5f + 0.5f) * static_cast<float>(Width), (0.5f - clip.Y / w * 0.5f) * static_cast<float>(Height) };
	}

	float RenderView::WorldPerPixel(Vec3 world) const
	{
		if (Orthographic)
			return 1.0f / PixelsPerUnit;
		// Projection(1,1) is 1 / tan(fov / 2), so this is the visible height at that depth over the pixel height.
		const float depth = std::fmax(Vec3::Dot(world - CameraPosition, CameraForward), 0.01f);
		return 2.0f * depth / (Projection.At(1, 1) * static_cast<float>(Height));
	}

	RenderView RenderView::FromCamera(const TransformComponent& transform, const CameraComponent& camera, uint32 width, uint32 height)
	{
		RenderView view;
		view.Width = width;
		view.Height = height;
		view.Background = camera.Background;
		view.CameraPosition = transform.Position;
		view.CameraForward = transform.GetForward();
		view.CameraRight = transform.GetRight();
		view.CameraUp = transform.GetUp();
		view.Orthographic = !camera.Perspective;
		view.Exposure = camera.Exposure;
		view.Tonemap = camera.Tonemap;

		// Cameras ignore scale so a scaled parent never skews the view.
		view.View = Mat4::TRS(transform.Position, transform.GetRotation(), { 1, 1, 1 }).Inverse();

		const float aspect = static_cast<float>(width) / static_cast<float>(height > 0 ? height : 1);
		if (camera.Perspective)
		{
			view.Projection = Mat4::Perspective(camera.FieldOfView, aspect, camera.NearClip, camera.FarClip);
		}
		else
		{
			view.PixelsPerUnit = camera.Zoom > 0.01f ? camera.Zoom : 0.01f;
			const float halfW = static_cast<float>(width) * 0.5f / view.PixelsPerUnit;
			const float halfH = static_cast<float>(height) * 0.5f / view.PixelsPerUnit;
			view.Projection = Mat4::Orthographic(-halfW, halfW, -halfH, halfH, -OrthoDepth, OrthoDepth);
		}
		return view;
	}

	RenderView RenderView::FromWorld(World& world, uint32 width, uint32 height)
	{
		if (const Entity camera = world.GetActiveCamera(); camera != NullEntity)
		{
			const TransformComponent transform = world.GetParent(camera) == NullEntity ? world.Get<TransformComponent>(camera) : world.GetWorldTransform(camera);
			RenderView view = FromCamera(transform, world.Get<CameraComponent>(camera), width, height);
			view.Camera = camera;
			return view;
		}
		return Orthographic2D({}, 64, width, height);
	}

	RenderView RenderView::Orthographic2D(Vec2 center, float pixelsPerUnit, uint32 width, uint32 height)
	{
		TransformComponent transform;
		transform.Position = { center, 0 };
		CameraComponent camera;
		camera.Zoom = pixelsPerUnit;
		return FromCamera(transform, camera, width, height);
	}
}
