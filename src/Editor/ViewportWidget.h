#pragma once

#include "Engine/Renderer2D.h"

#include <QImage>
#include <QWidget>

struct SDL_Surface;

namespace FP::Editor
{
	// Renders the world with SDL's software renderer into a surface that Qt paints.
	// Avoids embedding a native SDL window, which behaves differently on every OS.
	//
	// Edit mode: right or middle drag pans, wheel zooms, left click selects, left drag moves.
	// Play mode: shows the game camera and forwards keys and mouse to Input.
	class ViewportWidget : public QWidget
	{
		Q_OBJECT

	public:
		explicit ViewportWidget(QWidget* parent = nullptr);
		~ViewportWidget() override;

		void SetWorld(World* world) { m_World = world; }
		void SetSelected(Entity entity) { m_Selected = entity; }
		void SetPlaying(bool playing);

		// Draws one frame. The editor calls this from its tick.
		void RenderFrame();

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
		void RecreateSurface();
		ViewParams CurrentView() const;
		Vec2 ToPixels(QPointF point) const;
		Entity PickEntity(Vec2 worldPoint);

		World* m_World = nullptr;
		Entity m_Selected = NullEntity;
		bool m_Playing = false;

		SDL_Surface* m_Surface = nullptr;
		SDL_Renderer* m_SDLRenderer = nullptr;
		Renderer2D* m_Renderer = nullptr;
		QImage m_Image;

		// Editor camera.
		Vec2 m_CameraCenter;
		float m_PixelsPerUnit = 48;

		QPointF m_LastMouse;
		bool m_Panning = false;
		bool m_Dragging = false;
		Vec2 m_DragOffset;
	};
}
