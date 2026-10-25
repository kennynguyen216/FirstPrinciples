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

		// Circle spanned by two axes, the XY plane by default.
		void AddCircle(Vec3 center, float radius, Color color, int32 segments = 32, Vec3 axisA = { 1, 0, 0 }, Vec3 axisB = { 0, 1, 0 });

		// Box from local min to max, placed by transform.
		void AddBox(const Mat4& transform, Vec3 min, Vec3 max, Color color);

		// Outline a camera's clip volume. Also shows a frame when viewed from that camera.
		void AddFrustum(const RenderView& camera, Color color);

		// Grid on the y = 0 floor around center. X axis red, Z axis blue.
		void AddGrid3D(Vec3 center, float extent, float step);

		// Grid on the z = 0 plane covering what the view can see. Axes are tinted red (X) and green (Y).
		void AddGrid2D(const RenderView& view);

		void Clear() { m_Vertices.Clear(); }
		const Array<LineVertex>& GetVertices() const { return m_Vertices; }

	private:
		Array<LineVertex> m_Vertices;
	};
}
