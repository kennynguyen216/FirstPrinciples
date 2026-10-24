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

	void DebugLines::AddCircle(Vec3 center, float radius, Color color, int32 segments, Vec3 axisA, Vec3 axisB)
	{
		for (int32 i = 0; i < segments; ++i)
		{
			const float a0 = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * Pi;
			const float a1 = static_cast<float>(i + 1) / static_cast<float>(segments) * 2.0f * Pi;
			AddLine(center + (axisA * std::cos(a0) + axisB * std::sin(a0)) * radius,
				center + (axisA * std::cos(a1) + axisB * std::sin(a1)) * radius, color);
		}
	}

	void DebugLines::AddBox(const Mat4& transform, Vec3 min, Vec3 max, Color color)
	{
		Vec3 corners[8];
		for (int i = 0; i < 8; ++i)
			corners[i] = transform.TransformPoint({ (i & 1) ? max.X : min.X, (i & 2) ? max.Y : min.Y, (i & 4) ? max.Z : min.Z });
		// Each edge joins two corners that differ in exactly one bit.
		for (int i = 0; i < 8; ++i)
		{
			for (int bit : { 1, 2, 4 })
			{
				if (!(i & bit))
					AddLine(corners[i], corners[i | bit], color);
			}
		}
	}

	void DebugLines::AddGrid3D(Vec3 center, float extent, float step)
	{
		const float x0 = std::floor((center.X - extent) / step) * step;
		const float z0 = std::floor((center.Z - extent) / step) * step;
		const Color minor{ 1, 1, 1, 0.05f };
		for (float x = x0; x <= center.X + extent; x += step)
		{
			const bool axis = std::fabs(x) < step * 0.5f;
			AddLine({ x, 0, z0 }, { x, 0, z0 + extent * 2 }, axis ? Color{ 0.25f, 0.4f, 0.9f, 0.9f } : minor);
		}
		for (float z = z0; z <= center.Z + extent; z += step)
		{
			const bool axis = std::fabs(z) < step * 0.5f;
			AddLine({ x0, 0, z }, { x0 + extent * 2, 0, z }, axis ? Color{ 0.8f, 0.25f, 0.25f, 0.9f } : minor);
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
