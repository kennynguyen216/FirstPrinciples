#pragma once

#include "Engine/Render/RenderView.h"

namespace FP
{
	struct LineVertex
	{
		Vec3 Position;
		Color Color;
	};

	// Lines collected on the CPU and drawn on top of the scene. Used for the grid and selection.
	class DebugLines
	{
	public:
		void AddLine(Vec3 a, Vec3 b, Color color);

		// Rectangle of size centered on the transform, lying in its local XY plane.
		void AddRect(const Mat4& transform, Vec2 size, Color color);

		// Grid on the z = 0 plane covering what the view can see. Axes are tinted red (X) and green (Y).
		void AddGrid2D(const RenderView& view);

		void Clear() { m_Vertices.Clear(); }
		const Array<LineVertex>& GetVertices() const { return m_Vertices; }

	private:
		Array<LineVertex> m_Vertices;
	};
}
