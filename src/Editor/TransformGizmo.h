#pragma once

#include "Engine/Components.h"
#include "Engine/Render/RenderView.h"

class QPainter;

namespace FP::Editor
{
	enum class GizmoMode
	{
		Select, // no gizmo, dragging the entity moves it freely
		Move,
		Rotate,
		Scale,
	};

	// UE style handles drawn over the viewport for the selected entity.
	// Positions are in device pixels like RenderView. The painter works in logical pixels.
	class TransformGizmo
	{
	public:
		enum class Handle
		{
			None,
			X,
			Y,
			Z,
			XY,
			YZ,
			XZ,
			Center, // free move on the view plane, or uniform scale
		};

		void SetMode(GizmoMode mode) { m_Mode = mode; }
		GizmoMode GetMode() const { return m_Mode; }

		// World axes or the entity's own. Scale always uses the entity's own.
		void SetLocal(bool local) { m_Local = local; }
		bool IsLocal() const { return m_Local; }

		// 2D hides the Z handles since everything lives on the XY plane.
		void Set3D(bool enabled) { m_3D = enabled; }

		void SetHover(Handle handle) { m_Hover = handle; }

		bool IsVisible(const RenderView& view, const TransformComponent& transform) const;
		void Paint(QPainter& painter, const RenderView& view, float dpr, const TransformComponent& transform) const;
		Handle HitTest(const RenderView& view, float dpr, const TransformComponent& transform, Vec2 pixel) const;

		// Grabs handle at pixel. False when it cannot be dragged from this angle.
		bool BeginDrag(Handle handle, const RenderView& view, float dpr, const TransformComponent& transform, Vec2 pixel);

		// Applies the drag so far to transform. snap is the grid size for moves, 0 for none.
		// snapAngles turns rotation in 15 degree steps.
		void UpdateDrag(const RenderView& view, Vec2 pixel, TransformComponent& transform, float snap, bool snapAngles);

		void EndDrag() { m_Active = Handle::None; }
		bool IsDragging() const { return m_Active != Handle::None; }

	private:
		// Where the gizmo sits and which way its axes point, in world space.
		struct Frame
		{
			Vec3 Origin;
			Vec3 Axes[3];
			float Length = 1; // world units for the screen size of an arrow
		};

		Frame MakeFrame(const RenderView& view, float dpr, const TransformComponent& transform, GizmoMode mode) const;
		bool ShowsAxis(const RenderView& view, const Frame& frame, int axis) const;

		GizmoMode m_Mode = GizmoMode::Move;
		bool m_Local = false;
		bool m_3D = false;
		Handle m_Hover = Handle::None;

		// Drag state, captured when the drag starts.
		Handle m_Active = Handle::None;
		Frame m_StartFrame;
		TransformComponent m_Start;
		Vec2 m_StartPixel;
		Vec3 m_StartHit;
		float m_StartParam = 0;
		float m_LengthPixels = 1;
		float m_Angle = 0;
		float m_LastScreenAngle = 0;
	};
}
