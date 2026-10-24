#pragma once

#include "Engine/Render/Renderer.h"

#include <QImage>
#include <QWidget>

namespace FP::Editor
{
	// Renders the world on the GPU into an offscreen texture and paints the pixels with Qt.
	// Avoids embedding a native SDL window, which behaves differently on every OS.
	//
	// Edit mode: right or middle drag pans, wheel zooms, left click selects, left drag moves.
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

		// Draws one frame. The editor calls this from its tick.
		void RenderFrame();

		Renderer* GetRenderer() const { return m_Renderer; }

	signals:
		void EntityPicked(FP::Entity entity);
		void EntityMoved(FP::Entity entity);

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

	private:
		void RecreateTarget();
		RenderView CurrentView() const;
		Vec2 ToPixels(QPointF point) const;
		Vec2 PixelToWorld2D(Vec2 pixel) const;
		Entity PickEntity(Vec2 worldPoint);
		Entity PickGizmo(Vec2 pixel) const;
		void AddGizmos(const RenderView& view);

		GPUDevice& m_Device;
		Renderer* m_Renderer = nullptr;
		RefPtr<GPUTexture> m_Target;
		QImage m_Image;
		DebugLines m_Lines;

		World* m_World = nullptr;
		Entity m_Selected = NullEntity;
		bool m_Playing = false;

		// Editor camera.
		Vec2 m_CameraCenter;
		float m_PixelsPerUnit = 48;

		QPointF m_LastMouse;
		bool m_Panning = false;
		bool m_Dragging = false;
		Vec2 m_DragOffset;
	};
}
