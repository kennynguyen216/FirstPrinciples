#include "Tests/TestFramework.h"
#include "Engine/Render/DebugLines.h"
#include <limits>
using namespace FP;

FP_TEST(Meshlet_ObserverFrameAndPickingMatchBothProjections)
{
	for (bool perspective : { false, true })
	{
		RenderView camera;
		camera.Width = 800; camera.Height = 600;
		camera.Orthographic = !perspective;
		camera.Projection = perspective ? Mat4::Perspective(60, 4.0f / 3.0f, 0.1f, 100) : Mat4::Orthographic(-4, 6, -3, 3, 0.1f, 100);
		const auto observer = camera.Expanded(1.5f);
		const auto inverse = camera.GetViewProjection().Inverse();
		const Vec4 rightEdge = inverse * Vec4(1, 0, 0.5f, 1);
		const Vec3 worldEdge = rightEdge.XYZ() / rightEdge.W;
		const Vec2 pixel = observer.WorldToScreen(worldEdge);
		FP_EXPECT(std::fabs(pixel.X - 800.0f * (0.5f + 0.5f / 1.5f)) < 0.01f);
		FP_EXPECT(std::fabs(pixel.Y - 300) < 0.01f);
		const Ray ray = observer.ScreenToRay(pixel);
		const Vec3 toPoint = worldEdge - ray.Origin;
		FP_EXPECT(Vec3::Cross(toPoint, ray.Direction).Length() < 0.001f);
		FP_EXPECT(std::fabs(observer.PixelsPerUnit * 1.5f - camera.PixelsPerUnit) < 0.001f);
		DebugLines lines;
		lines.AddFrustum(camera, { 1, 1, 0, 1 });
		FP_EXPECT(lines.GetVertices().Num() == 24);
		for (const auto& vertex : lines.GetVertices())
		{
			const Vec4 clip = observer.GetViewProjection() * Vec4(vertex.Position, 1);
			FP_EXPECT(std::fabs(std::fabs(clip.X / clip.W) - 1.0f / 1.5f) < 0.001f);
			FP_EXPECT(std::fabs(std::fabs(clip.Y / clip.W) - 1.0f / 1.5f) < 0.001f);
		}
		const auto invalid = camera.Expanded(std::numeric_limits<float>::quiet_NaN());
		FP_EXPECT(invalid.Projection.At(0, 0) == camera.Projection.At(0, 0));
	}
}
