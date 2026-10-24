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
	}

	ViewportWidget::ViewportWidget(GPUDevice& device, QWidget* parent) : QWidget(parent), m_Device(device)
	{
		setFocusPolicy(Qt::StrongFocus);
		setMouseTracking(true);
		setMinimumSize(320, 240);
		setAttribute(Qt::WA_OpaquePaintEvent);

		// RGBA sRGB is a valid render target everywhere and matches QImage::Format_RGBA8888.
		m_Renderer = new Renderer(device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB);
	}

	ViewportWidget::~ViewportWidget()
	{
		m_Target.Reset();
		delete m_Renderer;
	}

	void ViewportWidget::SetPlaying(bool playing)
	{
		m_Playing = playing;
		m_Dragging = false;
		Input::Reset();
		if (playing)
			setFocus();
	}

	void ViewportWidget::RecreateTarget()
	{
		const qreal dpr = devicePixelRatioF();
		const uint32 width = static_cast<uint32>(std::max(1L, std::lround(this->width() * dpr)));
		const uint32 height = static_cast<uint32>(std::max(1L, std::lround(this->height() * dpr)));
		if (m_Target && m_Target->GetWidth() == width && m_Target->GetHeight() == height)
			return;

		m_Target = m_Device.CreateRenderTarget(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB, width, height, false);
		m_Image = QImage(static_cast<int>(width), static_cast<int>(height), QImage::Format_RGBA8888);
		m_Image.setDevicePixelRatio(dpr);
	}

	RenderView ViewportWidget::CurrentView() const
	{
		const uint32 width = m_Target ? m_Target->GetWidth() : 1;
		const uint32 height = m_Target ? m_Target->GetHeight() : 1;
		if (m_Playing && m_World)
			return RenderView::FromWorld(*m_World, width, height);

		RenderView view = RenderView::Orthographic2D(m_CameraCenter, m_PixelsPerUnit * static_cast<float>(devicePixelRatioF()), width, height);
		view.Background = { 0.12f, 0.12f, 0.14f, 1 };
		return view;
	}

	Vec2 ViewportWidget::ToPixels(QPointF point) const
	{
		const float dpr = static_cast<float>(devicePixelRatioF());
		return { static_cast<float>(point.x()) * dpr, static_cast<float>(point.y()) * dpr };
	}

	Vec2 ViewportWidget::PixelToWorld2D(Vec2 pixel) const
	{
		Vec3 point;
		const Ray ray = CurrentView().ScreenToRay(pixel);
		if (!ray.IntersectZPlane(0, point))
			point = ray.Origin;
		return point.XY();
	}

	void ViewportWidget::RenderFrame()
	{
		RecreateTarget();
		if (!m_Renderer->IsValid() || !m_World || !m_Target)
			return;

		const RenderView view = CurrentView();
		m_Lines.Clear();
		if (!m_Playing)
		{
			m_Lines.AddGrid2D(view);
			if (m_World->IsValid(m_Selected) && m_World->Has<TransformComponent>(m_Selected))
			{
				const TransformComponent& transform = m_World->Get<TransformComponent>(m_Selected);
				const SpriteComponent* sprite = m_World->TryGet<SpriteComponent>(m_Selected);
				const Vec2 size = sprite ? sprite->Size : Vec2(1, 1);
				// Three rings a pixel apart, just outside the sprite, so the outline reads at any zoom.
				const Mat4 model = transform.GetMatrix();
				for (int ring = 1; ring <= 3; ++ring)
				{
					const float grow = static_cast<float>(ring) * 2.0f / view.PixelsPerUnit;
					const Vec2 scale{ std::fabs(transform.Scale.X), std::fabs(transform.Scale.Y) };
					const Vec2 pad{ scale.X > 1e-4f ? grow / scale.X : 0, scale.Y > 1e-4f ? grow / scale.Y : 0 };
					m_Lines.AddRect(model, size + pad, { 1, 0.85f, 0.1f, 1 });
				}
			}
		}

		SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(m_Device.GetHandle());
		m_Renderer->Render(cmd, m_Target->GetHandle(), m_Target->GetWidth(), m_Target->GetHeight(), *m_World, view, &m_Lines);
		SDL_SubmitGPUCommandBuffer(cmd);
		m_Device.ReadPixels(m_Target->GetHandle(), m_Target->GetWidth(), m_Target->GetHeight(), m_Image.bits());
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
			const Mat4 toLocal = (transform.GetMatrix() * Mat4::Scale({ sprite.Size, 1 })).Inverse();
			const Vec3 local = toLocal.TransformPoint({ worldPoint, transform.Position.Z });
			if (std::fabs(local.X) <= 0.5f && std::fabs(local.Y) <= 0.5f && sprite.Layer >= bestLayer)
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
			const Vec2 worldPoint = PixelToWorld2D(ToPixels(event->position()));
			const Entity picked = PickEntity(worldPoint);
			m_Selected = picked;
			emit EntityPicked(picked);
			if (picked != NullEntity)
			{
				m_Dragging = true;
				m_DragOffset = m_World->Get<TransformComponent>(picked).Position.XY() - worldPoint;
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
			Vec2 target = PixelToWorld2D(ToPixels(position)) + m_DragOffset;
			// Hold shift to snap to a quarter unit grid.
			if (event->modifiers() & Qt::ShiftModifier)
				target = { std::round(target.X * 4) / 4, std::round(target.Y * 4) / 4 };
			Vec3& position3 = m_World->Get<TransformComponent>(m_Selected).Position;
			position3 = { target, position3.Z };
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
		const Vec2 before = PixelToWorld2D(pixel);
		const float factor = std::pow(1.0015f, static_cast<float>(event->angleDelta().y()));
		m_PixelsPerUnit = std::clamp(m_PixelsPerUnit * factor, 4.0f, 1024.0f);
		const Vec2 after = PixelToWorld2D(pixel);
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
