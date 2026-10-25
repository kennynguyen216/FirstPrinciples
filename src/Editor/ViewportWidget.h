#pragma once

#include "Editor/TransformGizmo.h"
#include "Engine/Render/Renderer.h"

#include <QImage>
#include <QWidget>

#include <optional>

namespace FP::Editor
{
	// Renders the world on the GPU into an offscreen texture and paints the pixels with Qt.
	// Avoids embedding a native SDL window, which behaves differently on every OS.
	//
	// 2D edit mode: right or middle drag pans, wheel zooms.
	// 3D edit mode: right drag looks around, Alt left drag orbits, middle drag (or Shift right drag) pans,
	// wheel dollies.
	// Both: hold the right button and use WASD to fly, Q and E for down and up, Shift to go faster and the
	// wheel to change speed. Left click selects, F focuses the selection. The gizmo moves, rotates or
	// scales the selection, and Q, W, E and R switch between select, move, rotate and scale.
	// Play mode: shows the game camera and forwards keys and mouse to Input.
	class ViewportWidget : public QWidget
	{
		Q_OBJECT

	public:
		ViewportWidget(GPUDevice& device, QWidget* parent = nullptr);
		~ViewportWidget() override;

		void SetWorld(World* world) { m_World = world; }
		void SetSelected(Entity entity) { m_Selected = entity; }
		void SetPlaying(bool playing);
		// Hides and holds the cursor while the game wants it locked. Call every frame, after the world update.
		void ApplyCursorLock();

		void Set3D(bool enabled) { m_3D = enabled; m_Gizmo.Set3D(enabled); }
		bool Is3D() const { return m_3D; }

		// Lit, Unlit or Wireframe. Applies in edit and play so the game can be inspected while it runs.
		void SetViewMode(ViewMode mode) { m_ViewMode = mode; }
		ViewMode GetViewMode() const { return m_ViewMode; }

		// Snaps dragged entities to a grid of this size. Shift inverts it while dragging.
		void SetSnap(bool enabled, float size) { m_SnapEnabled = enabled; m_SnapSize = size; }

		void SetGizmoMode(GizmoMode mode);
		GizmoMode GetGizmoMode() const { return m_Gizmo.GetMode(); }

		// Move and rotate along the entity's own axes instead of the world's.
		void SetGizmoLocal(bool local) { m_Gizmo.SetLocal(local); update(); }

		// A widget that floats along the top edge, like the viewport toolbar. The viewport keeps it sized.
		void SetOverlay(QWidget* overlay);

		// Centers the editor camera on the selected entity.
		void FocusSelected();

		// Draws one frame. The editor calls this from its tick.
		void RenderFrame();

		Renderer* GetRenderer() const { return m_Renderer; }

	signals:
		void EntityPicked(FP::Entity entity);
		void EntityMoved(FP::Entity entity);

		// A key switched the gizmo, so buttons showing the mode can follow.
		void GizmoModeChanged(FP::Editor::GizmoMode mode);

		// A file was dropped. point is where it landed in the world and target is what was under it.
		void AssetDropped(const QString& path, FP::Vec3 point, FP::Entity target);

		// An entry from Place Entities was dropped.
		void EntityDropped(const QString& kind, FP::Vec3 point);

	protected:
		void paintEvent(QPaintEvent* event) override;
		void resizeEvent(QResizeEvent* event) override;
		void mousePressEvent(QMouseEvent* event) override;
		void mouseReleaseEvent(QMouseEvent* event) override;
		void mouseMoveEvent(QMouseEvent* event) override;
		void wheelEvent(QWheelEvent* event) override;
		void keyPressEvent(QKeyEvent* event) override;
		void keyReleaseEvent(QKeyEvent* event) override;
		void focusOutEvent(QFocusEvent* event) override;
		void dragEnterEvent(QDragEnterEvent* event) override;
		void dropEvent(QDropEvent* event) override;

	private:
		void RecreateTarget();
		void RecenterCursor();
		RenderView CurrentView() const;
		Vec2 ToPixels(QPointF point) const;
		Vec2 PixelToWorld2D(Vec2 pixel) const;

		// Gizmos first, then the surface the ray hits. 2D prefers the highest sprite layer.
		Entity Pick(Vec2 pixel) const;
		Entity PickGizmo(Vec2 pixel) const;
		// The selected entity's world transform, or nothing when there is none.
		std::optional<TransformComponent> SelectedTransform() const;

		// The 3D editor camera, placed behind the orbit target.
		TransformComponent EditorCamera() const;

		// Moves the camera for held WASD keys while the right button is down.
		void Fly(float deltaTime);
		// The GPU mesh of a Mesh or SkeletalMesh component, or null.
		const GPUMesh* FindMesh(Entity entity) const;
		bool HitSurface(const Ray& ray, Entity entity, float& outDistance) const;

		void AddGizmos(const RenderView& view);
		void AddSelection(const RenderView& view);
		// Outlines every collider's shape, the selected one brighter.
		void AddColliders();

		GPUDevice& m_Device;
		Renderer* m_Renderer = nullptr;
		RefPtr<GPUTexture> m_Target;
		QImage m_Image;
		DebugLines m_Lines;

		World* m_World = nullptr;
		Entity m_Selected = NullEntity;
		bool m_Playing = false;
		bool m_CursorLocked = false;
		ViewMode m_ViewMode = ViewMode::Lit;

		// 2D editor camera.
		Vec2 m_CameraCenter;
		float m_PixelsPerUnit = 48;

		// 3D editor camera orbits a target point.
		bool m_3D = false;
		Vec3 m_OrbitTarget{ 0, 0.5f, 0 };
		float m_Yaw = 0;
		float m_Pitch = -25;
		float m_Distance = 10;

		QPointF m_LastMouse;
		bool m_Panning = false;
		bool m_Orbiting = false;
		bool m_Looking = false;
		bool m_Dragging = false;

		TransformGizmo m_Gizmo;

		// Right button held, so WASD flies instead of switching gizmo modes.
		bool m_Flying = false;
		uint32 m_FlyKeys = 0;
		bool m_FlyFast = false;
		float m_FlySpeed = 5;
		uint64 m_LastFrameTicks = 0;

		QWidget* m_Overlay = nullptr;
		bool m_SnapEnabled = false;
		float m_SnapSize = 0.25f;

		// Dragging moves the selection along a plane facing the camera through where it was grabbed.
		Vec3 m_DragPlanePoint;
		Vec3 m_DragPlaneNormal;
		Vec3 m_DragOffset;
	};
}
