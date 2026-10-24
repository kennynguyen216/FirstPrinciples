#include "Editor/ViewportWidget.h"
#include "Engine/Input.h"

#include <SDL3/SDL.h>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <cmath>

namespace FP::Editor
{
	namespace
	{
		// Maps the keys games usually need from Qt to SDL scancodes.
		Key ToScancode(int qtKey)
		{
			if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z)
				return static_cast<Key>(SDL_SCANCODE_A + (qtKey - Qt::Key_A));
			if (qtKey >= Qt::Key_1 && qtKey <= Qt::Key_9)
				return static_cast<Key>(SDL_SCANCODE_1 + (qtKey - Qt::Key_1));
			if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F12)
				return static_cast<Key>(SDL_SCANCODE_F1 + (qtKey - Qt::Key_F1));
			switch (qtKey)
			{
				case Qt::Key_0: return SDL_SCANCODE_0;
				case Qt::Key_Left: return SDL_SCANCODE_LEFT;
				case Qt::Key_Right: return SDL_SCANCODE_RIGHT;
				case Qt::Key_Up: return SDL_SCANCODE_UP;
				case Qt::Key_Down: return SDL_SCANCODE_DOWN;
				case Qt::Key_Space: return SDL_SCANCODE_SPACE;
				case Qt::Key_Return:
				case Qt::Key_Enter: return SDL_SCANCODE_RETURN;
				case Qt::Key_Escape: return SDL_SCANCODE_ESCAPE;
				case Qt::Key_Tab: return SDL_SCANCODE_TAB;
				case Qt::Key_Backspace: return SDL_SCANCODE_BACKSPACE;
				case Qt::Key_Shift: return SDL_SCANCODE_LSHIFT;
				case Qt::Key_Control: return SDL_SCANCODE_LCTRL;
				case Qt::Key_Alt: return SDL_SCANCODE_LALT;
				default: return SDL_SCANCODE_UNKNOWN;
			}
		}

		bool PointInRect(Vec2 point, Vec2 center, Vec2 size, float rotation)
		{
			const Vec2 local = (point - center).Rotated(-rotation);
			return std::fabs(local.X) <= size.X * 0.5f && std::fabs(local.Y) <= size.Y * 0.5f;
		}
	}

	ViewportWidget::ViewportWidget(QWidget* parent) : QWidget(parent)
	{
		setFocusPolicy(Qt::StrongFocus);
		setMouseTracking(true);
		setMinimumSize(320, 240);
		setAttribute(Qt::WA_OpaquePaintEvent);
	}

	ViewportWidget::~ViewportWidget()
	{
		delete m_Renderer;
		if (m_SDLRenderer)
			SDL_DestroyRenderer(m_SDLRenderer);
		if (m_Surface)
			SDL_DestroySurface(m_Surface);
	}

	void ViewportWidget::SetPlaying(bool playing)
	{
		m_Playing = playing;
		m_Dragging = false;
		Input::Reset();
		if (playing)
			setFocus();
	}

	void ViewportWidget::RecreateSurface()
	{
		const qreal dpr = devicePixelRatioF();
		const int width = std::max(1, static_cast<int>(std::lround(this->width() * dpr)));
		const int height = std::max(1, static_cast<int>(std::lround(this->height() * dpr)));
		if (m_Surface && m_Surface->w == width && m_Surface->h == height)
			return;

		// Textures belong to the old renderer so the whole renderer is rebuilt with the surface.
		delete m_Renderer;
		m_Renderer = nullptr;
		if (m_SDLRenderer)
			SDL_DestroyRenderer(m_SDLRenderer);
		if (m_Surface)
			SDL_DestroySurface(m_Surface);

		m_Surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_XRGB8888);
		m_SDLRenderer = m_Surface ? SDL_CreateSoftwareRenderer(m_Surface) : nullptr;
		if (!m_SDLRenderer)
		{
			FP_LOG_ERROR("Viewport could not create a software renderer: %s", SDL_GetError());
			return;
		}
		m_Renderer = new Renderer2D(m_SDLRenderer);

		// Shares the pixels with SDL so painting needs no copy.
		m_Image = QImage(static_cast<uchar*>(m_Surface->pixels), width, height, m_Surface->pitch, QImage::Format_RGB32);
		m_Image.setDevicePixelRatio(dpr);
	}

	ViewParams ViewportWidget::CurrentView() const
	{
		const int32 width = m_Surface ? m_Surface->w : 1;
		const int32 height = m_Surface ? m_Surface->h : 1;
		if (m_Playing && m_World)
			return Renderer2D::GetCameraView(*m_World, width, height);

		ViewParams view;
		view.Width = width;
		view.Height = height;
		view.Center = m_CameraCenter;
		view.PixelsPerUnit = m_PixelsPerUnit * static_cast<float>(devicePixelRatioF());
		view.Background = { 0.12f, 0.12f, 0.14f, 1 };
		return view;
	}

	Vec2 ViewportWidget::ToPixels(QPointF point) const
	{
		const float dpr = static_cast<float>(devicePixelRatioF());
		return { static_cast<float>(point.x()) * dpr, static_cast<float>(point.y()) * dpr };
	}

	void ViewportWidget::RenderFrame()
	{
		RecreateSurface();
		if (!m_Renderer || !m_World)
			return;

		const ViewParams view = CurrentView();
		m_Renderer->Render(*m_World, view, !m_Playing);
		if (!m_Playing)
		{
			if (m_World->IsValid(m_Selected) && m_World->Has<TransformComponent>(m_Selected))
			{
				const TransformComponent& transform = m_World->Get<TransformComponent>(m_Selected);
				const SpriteComponent* sprite = m_World->TryGet<SpriteComponent>(m_Selected);
				const Vec2 size = (sprite ? sprite->Size : Vec2(1, 1)) * transform.Scale;
				m_Renderer->DrawRectOutline(transform.Position, size, transform.Rotation, { 1, 0.85f, 0.1f, 1 }, view, 3);
			}
		}
		SDL_RenderPresent(m_SDLRenderer);
		update();
	}

	void ViewportWidget::paintEvent(QPaintEvent*)
	{
		QPainter painter(this);
		if (m_Image.isNull())
			painter.fillRect(rect(), Qt::black);
		else
			painter.drawImage(QPointF(0, 0), m_Image);
	}

	void ViewportWidget::resizeEvent(QResizeEvent*)
	{
		RenderFrame();
	}

	Entity ViewportWidget::PickEntity(Vec2 worldPoint)
	{
		// Topmost sprite under the point wins, matching draw order.
		Entity best = NullEntity;
		int32 bestLayer = INT32_MIN;
		for (auto [entity, transform, sprite] : m_World->View<TransformComponent, SpriteComponent>().each())
		{
			if (sprite.Layer >= bestLayer && PointInRect(worldPoint, transform.Position, sprite.Size * transform.Scale, transform.Rotation))
			{
				best = entity;
				bestLayer = sprite.Layer;
			}
		}
		return best;
	}

	void ViewportWidget::mousePressEvent(QMouseEvent* event)
	{
		setFocus();
		m_LastMouse = event->position();
		if (m_Playing)
		{
			if (event->button() == Qt::LeftButton) Input::SetMouseButton(MouseButton::Left, true);
			if (event->button() == Qt::RightButton) Input::SetMouseButton(MouseButton::Right, true);
			if (event->button() == Qt::MiddleButton) Input::SetMouseButton(MouseButton::Middle, true);
			return;
		}
		if (!m_World)
			return;

		if (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton)
		{
			m_Panning = true;
		}
		else if (event->button() == Qt::LeftButton)
		{
			const Vec2 worldPoint = CurrentView().ScreenToWorld(ToPixels(event->position()));
			const Entity picked = PickEntity(worldPoint);
			m_Selected = picked;
			emit EntityPicked(picked);
			if (picked != NullEntity)
			{
				m_Dragging = true;
				m_DragOffset = m_World->Get<TransformComponent>(picked).Position - worldPoint;
			}
		}
	}

	void ViewportWidget::mouseReleaseEvent(QMouseEvent* event)
	{
		if (m_Playing)
		{
			if (event->button() == Qt::LeftButton) Input::SetMouseButton(MouseButton::Left, false);
			if (event->button() == Qt::RightButton) Input::SetMouseButton(MouseButton::Right, false);
			if (event->button() == Qt::MiddleButton) Input::SetMouseButton(MouseButton::Middle, false);
			return;
		}
		m_Panning = false;
		m_Dragging = false;
	}

	void ViewportWidget::mouseMoveEvent(QMouseEvent* event)
	{
		const QPointF position = event->position();
		if (m_Playing)
		{
			Input::SetMousePosition(ToPixels(position));
			return;
		}

		if (m_Panning)
		{
			const QPointF delta = position - m_LastMouse;
			m_CameraCenter -= Vec2(static_cast<float>(delta.x()), -static_cast<float>(delta.y())) / m_PixelsPerUnit;
		}
		else if (m_Dragging && m_World && m_World->IsValid(m_Selected))
		{
			Vec2 target = CurrentView().ScreenToWorld(ToPixels(position)) + m_DragOffset;
			// Hold shift to snap to a quarter unit grid.
			if (event->modifiers() & Qt::ShiftModifier)
				target = { std::round(target.X * 4) / 4, std::round(target.Y * 4) / 4 };
			m_World->Get<TransformComponent>(m_Selected).Position = target;
			emit EntityMoved(m_Selected);
		}
		m_LastMouse = position;
	}

	void ViewportWidget::wheelEvent(QWheelEvent* event)
	{
		if (m_Playing)
			return;
		// Zoom around the cursor so the point under it stays put.
		const Vec2 pixel = ToPixels(event->position());
		const Vec2 before = CurrentView().ScreenToWorld(pixel);
		const float factor = std::pow(1.0015f, static_cast<float>(event->angleDelta().y()));
		m_PixelsPerUnit = std::clamp(m_PixelsPerUnit * factor, 4.0f, 1024.0f);
		const Vec2 after = CurrentView().ScreenToWorld(pixel);
		m_CameraCenter += before - after;
	}

	void ViewportWidget::keyPressEvent(QKeyEvent* event)
	{
		if (m_Playing && !event->isAutoRepeat())
			Input::SetKey(ToScancode(event->key()), true);
		else
			QWidget::keyPressEvent(event);
	}

	void ViewportWidget::keyReleaseEvent(QKeyEvent* event)
	{
		if (m_Playing && !event->isAutoRepeat())
			Input::SetKey(ToScancode(event->key()), false);
		else
			QWidget::keyReleaseEvent(event);
	}

	void ViewportWidget::focusOutEvent(QFocusEvent* event)
	{
		Input::Reset();
		QWidget::focusOutEvent(event);
	}
}
