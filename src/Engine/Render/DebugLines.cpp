#include "Engine/Render/DebugLines.h"

namespace FP
{
	void DebugLines::AddLine(Vec3 a, Vec3 b, Color color)
	{
		m_Vertices.Add({ a, color });
		m_Vertices.Add({ b, color });
	}

	void DebugLines::AddRect(const Mat4& transform, Vec2 size, Color color)
	{
		const Vec2 h = size * 0.5f;
		const Vec3 corners[4] = {
			transform.TransformPoint({ -h.X, h.Y, 0 }),
			transform.TransformPoint({ h.X, h.Y, 0 }),
			transform.TransformPoint({ h.X, -h.Y, 0 }),
			transform.TransformPoint({ -h.X, -h.Y, 0 }),
		};
		for (int i = 0; i < 4; ++i)
			AddLine(corners[i], corners[(i + 1) % 4], color);
	}

	void DebugLines::AddCircle(Vec3 center, float radius, Color color, int32 segments)
	{
		for (int32 i = 0; i < segments; ++i)
		{
			const float a0 = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * Pi;
			const float a1 = static_cast<float>(i + 1) / static_cast<float>(segments) * 2.0f * Pi;
			AddLine(center + Vec3(std::cos(a0), std::sin(a0), 0) * radius, center + Vec3(std::cos(a1), std::sin(a1), 0) * radius, color);
		}
	}

	void DebugLines::AddGrid2D(const RenderView& view)
	{
		Vec3 min;
		Vec3 max;
		const Vec2 w{ static_cast<float>(view.Width), static_cast<float>(view.Height) };
		if (!view.ScreenToRay({ 0, w.Y }).IntersectZPlane(0, min) && !view.Orthographic)
			return;
		view.ScreenToRay({ w.X, 0 }).IntersectZPlane(0, max);
		if (view.Orthographic)
		{
			min = view.ScreenToRay({ 0, w.Y }).Origin;
			max = view.ScreenToRay({ w.X, 0 }).Origin;
		}

		// Skip lines when zoomed far out so the grid stays readable.
		float step = 1;
		while (step * view.PixelsPerUnit < 16)
			step *= 2;

		const Color minor{ 1, 1, 1, 0.025f };
		for (float x = std::floor(min.X / step) * step; x <= max.X; x += step)
		{
			const bool axis = std::fabs(x) < step * 0.5f;
			AddLine({ x, min.Y, 0 }, { x, max.Y, 0 }, axis ? Color{ 0.2f, 0.7f, 0.2f, 0.9f } : minor);
		}
		for (float y = std::floor(min.Y / step) * step; y <= max.Y; y += step)
		{
			const bool axis = std::fabs(y) < step * 0.5f;
			AddLine({ min.X, y, 0 }, { max.X, y, 0 }, axis ? Color{ 0.7f, 0.2f, 0.2f, 0.9f } : minor);
		}
	}
}
