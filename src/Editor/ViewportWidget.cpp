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
		{
			RenderView game = RenderView::FromWorld(*m_World, width, height);
			game.Mode = m_ViewMode;
			return game;
		}

		RenderView view;
		if (m_3D)
		{
			TransformComponent transform;
			transform.Rotation = { m_Pitch, m_Yaw, 0 };
			transform.Position = m_OrbitTarget - transform.GetForward() * m_Distance;
			CameraComponent camera;
			camera.Perspective = true;
			camera.FieldOfView = 60;
			camera.NearClip = 0.05f;
			camera.FarClip = 5000;
			view = RenderView::FromCamera(transform, camera, width, height);
		}
		else
		{
			view = RenderView::Orthographic2D(m_CameraCenter, m_PixelsPerUnit * static_cast<float>(devicePixelRatioF()), width, height);
		}
		view.Background = { 0.12f, 0.12f, 0.14f, 1 };
		view.Mode = m_ViewMode;
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

	void ViewportWidget::FocusSelected()
	{
		if (!m_World || !m_World->IsValid(m_Selected) || !m_World->Has<TransformComponent>(m_Selected))
			return;
		const Vec3 position = m_World->Get<TransformComponent>(m_Selected).Position;
		m_CameraCenter = position.XY();
		m_OrbitTarget = position;
	}

	void ViewportWidget::AddGizmos(const RenderView& view)
	{
		const float dpr = static_cast<float>(devicePixelRatioF());

		for (auto [entity, transform, light] : m_World->View<TransformComponent, PointLightComponent>().each())
		{
			const Color color{ light.Color.R, light.Color.G, light.Color.B, 0.9f };
			const Vec3 p = transform.Position;
			const float handle = 10.0f * dpr * view.WorldPerPixel(p);
			m_Lines.AddCircle(p, handle, color, 16, view.CameraRight, view.CameraUp);
			m_Lines.AddLine(p - view.CameraRight * handle, p + view.CameraRight * handle, color);
			m_Lines.AddLine(p - view.CameraUp * handle, p + view.CameraUp * handle, color);
			if (entity == m_Selected)
			{
				// Range as three rings so it reads as a sphere in 3D and a circle in 2D.
				const Color faint{ color.R, color.G, color.B, 0.35f };
				m_Lines.AddCircle(p, light.Range, faint, 64, { 1, 0, 0 }, { 0, 1, 0 });
				if (m_3D)
				{
					m_Lines.AddCircle(p, light.Range, faint, 64, { 1, 0, 0 }, { 0, 0, 1 });
					m_Lines.AddCircle(p, light.Range, faint, 64, { 0, 1, 0 }, { 0, 0, 1 });
				}
			}
		}

		for (auto [entity, transform, light] : m_World->View<TransformComponent, DirectionalLightComponent>().each())
		{
			const Color color{ light.Color.R, light.Color.G, light.Color.B, 0.9f };
			const Vec3 p = transform.Position;
			const float handle = 10.0f * dpr * view.WorldPerPixel(p);
			m_Lines.AddCircle(p, handle, color, 16, view.CameraRight, view.CameraUp);
			const Vec3 forward = transform.GetForward();
			const Vec3 tip = p + forward * handle * 4.0f;
			m_Lines.AddLine(p, tip, color);
			m_Lines.AddLine(tip, tip - (forward + transform.GetRight() * 0.5f) * handle, color);
			m_Lines.AddLine(tip, tip - (forward - transform.GetRight() * 0.5f) * handle, color);
		}

		for (auto [entity, transform, camera] : m_World->View<TransformComponent, CameraComponent>().each())
		{
			const Color color{ 0.8f, 0.8f, 0.8f, 0.7f };
			const Vec3 p = transform.Position;
			const float handle = 10.0f * dpr * view.WorldPerPixel(p);
			const Mat4 rotation = Mat4::Translation(p) * Mat4::Rotation(transform.GetRotation());
			m_Lines.AddBox(rotation, Vec3(-1, -0.7f, -0.5f) * handle, Vec3(1, 0.7f, 0.5f) * handle, color);
			m_Lines.AddLine(p, p + transform.GetForward() * handle * 3.0f, color);
			// An orthographic camera also shows what it will see at the default 1280x720 window.
			if (!camera.Perspective && camera.Zoom > 0.01f)
				m_Lines.AddRect(rotation, { 1280.0f / camera.Zoom, 720.0f / camera.Zoom }, { color.R, color.G, color.B, 0.25f });
		}
	}

	void ViewportWidget::AddSelection(const RenderView& view)
	{
		if (!m_World->IsValid(m_Selected) || !m_World->Has<TransformComponent>(m_Selected))
			return;
		const Color color{ 1, 0.85f, 0.1f, 1 };
		const TransformComponent& transform = m_World->Get<TransformComponent>(m_Selected);
		const Mat4 model = transform.GetMatrix();

		if (const MeshComponent* mesh = m_World->TryGet<MeshComponent>(m_Selected))
		{
			if (const GPUMesh* gpuMesh = m_Renderer->GetMeshes().Get(mesh->Mesh))
			{
				// Pad by a few pixels so the box sits just outside the surface.
				const Vec3 pad = Vec3(1, 1, 1) * (3.0f * view.WorldPerPixel(transform.Position));
				const Vec3 scale{ std::fmax(std::fabs(transform.Scale.X), 1e-4f), std::fmax(std::fabs(transform.Scale.Y), 1e-4f), std::fmax(std::fabs(transform.Scale.Z), 1e-4f) };
				const Vec3 localPad{ pad.X / scale.X, pad.Y / scale.Y, pad.Z / scale.Z };
				m_Lines.AddBox(model, gpuMesh->BoundsMin - localPad, gpuMesh->BoundsMax + localPad, color);
			}
		}

		if (const SpriteComponent* sprite = m_World->TryGet<SpriteComponent>(m_Selected))
		{
			// Three rings a pixel apart, just outside the sprite, so the outline reads at any zoom.
			const float pixel = view.WorldPerPixel(transform.Position);
			for (int ring = 1; ring <= 3; ++ring)
			{
				const float grow = static_cast<float>(ring) * 2.0f * pixel;
				const Vec2 scale{ std::fabs(transform.Scale.X), std::fabs(transform.Scale.Y) };
				const Vec2 pad{ scale.X > 1e-4f ? grow / scale.X : 0, scale.Y > 1e-4f ? grow / scale.Y : 0 };
				m_Lines.AddRect(model, sprite->Size + pad, color);
			}
		}
	}

	Entity ViewportWidget::PickGizmo(Vec2 pixel) const
	{
		const RenderView view = CurrentView();
		const float radius = 12.0f * static_cast<float>(devicePixelRatioF());
		Entity best = NullEntity;
		float bestDistance = radius;
		auto consider = [&](Entity entity, const TransformComponent& transform) {
			// Skip things behind a perspective camera.
			if (!view.Orthographic && Vec3::Dot(transform.Position - view.CameraPosition, view.CameraForward) <= 0)
				return;
			const float distance = (view.WorldToScreen(transform.Position) - pixel).Length();
			if (distance < bestDistance)
			{
				best = entity;
				bestDistance = distance;
			}
		};
		for (auto [entity, transform, light] : m_World->View<TransformComponent, PointLightComponent>().each())
			consider(entity, transform);
		for (auto [entity, transform, light] : m_World->View<TransformComponent, DirectionalLightComponent>().each())
			consider(entity, transform);
		for (auto [entity, transform, camera] : m_World->View<TransformComponent, CameraComponent>().each())
			consider(entity, transform);
		return best;
	}

	bool ViewportWidget::HitSurface(const Ray& ray, Entity entity, float& outDistance) const
	{
		const TransformComponent& transform = m_World->Get<TransformComponent>(entity);
		const SpriteComponent* sprite = m_World->TryGet<SpriteComponent>(entity);
		const MeshComponent* mesh = m_World->TryGet<MeshComponent>(entity);

		Vec3 min{ -0.5f, -0.5f, 0 };
		Vec3 max{ 0.5f, 0.5f, 0 };
		Mat4 model = transform.GetMatrix();
		if (sprite)
		{
			model = model * Mat4::Scale({ sprite->Size, 1 });
		}
		else if (const GPUMesh* gpuMesh = mesh ? m_Renderer->GetMeshes().Get(mesh->Mesh) : nullptr)
		{
			min = gpuMesh->BoundsMin;
			max = gpuMesh->BoundsMax;
		}
		else
		{
			return false;
		}

		// Slab test in the object's local space. Flat sprites get a sliver of thickness.
		const Mat4 toLocal = model.Inverse();
		const Vec3 origin = toLocal.TransformPoint(ray.Origin);
		const Vec3 direction = toLocal.TransformVector(ray.Direction);
		float tMin = -1e30f;
		float tMax = 1e30f;
		const float o[3] = { origin.X, origin.Y, origin.Z };
		const float d[3] = { direction.X, direction.Y, direction.Z };
		const float lo[3] = { min.X, min.Y, min.Z - 1e-3f };
		const float hi[3] = { max.X, max.Y, max.Z + 1e-3f };
		for (int axis = 0; axis < 3; ++axis)
		{
			if (std::fabs(d[axis]) < 1e-9f)
			{
				if (o[axis] < lo[axis] || o[axis] > hi[axis])
					return false;
				continue;
			}
			float t0 = (lo[axis] - o[axis]) / d[axis];
			float t1 = (hi[axis] - o[axis]) / d[axis];
			if (t0 > t1)
				std::swap(t0, t1);
			tMin = std::fmax(tMin, t0);
			tMax = std::fmin(tMax, t1);
			if (tMin > tMax)
				return false;
		}
		if (tMax < 0)
			return false;
		// Back to world distance along the ray.
		const Vec3 hit = model.TransformPoint(origin + direction * std::fmax(tMin, 0.0f));
		outDistance = (hit - ray.Origin).Length();
		return true;
	}

	Entity ViewportWidget::Pick(Vec2 pixel) const
	{
		const Entity gizmo = PickGizmo(pixel);
		if (gizmo != NullEntity)
			return gizmo;

		const Ray ray = CurrentView().ScreenToRay(pixel);
		Entity best = NullEntity;
		float bestDistance = 1e30f;
		int32 bestLayer = INT32_MIN;
		auto consider = [&](Entity entity, int32 layer) {
			float distance = 0;
			if (!HitSurface(ray, entity, distance))
				return;
			// 2D picks what draws on top. 3D picks what is closest.
			const bool better = m_3D ? distance < bestDistance : (layer > bestLayer || (layer == bestLayer && distance < bestDistance));
			if (better)
			{
				best = entity;
				bestDistance = distance;
				bestLayer = layer;
			}
		};
		for (auto [entity, transform, sprite] : m_World->View<TransformComponent, SpriteComponent>().each())
			consider(entity, sprite.Layer);
		for (auto [entity, transform, mesh] : m_World->View<TransformComponent, MeshComponent>().each())
			consider(entity, 0);
		return best;
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
			if (m_3D)
			{
				// Grid step grows with distance so it never turns into a solid sheet.
				float step = 1;
				while (step * 40 < m_Distance)
					step *= 2;
				m_Lines.AddGrid3D(m_OrbitTarget, step * 40, step);
			}
			else
			{
				m_Lines.AddGrid2D(view);
			}
			AddGizmos(view);
			AddSelection(view);
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

		const bool alt = event->modifiers() & Qt::AltModifier;
		const bool shift = event->modifiers() & Qt::ShiftModifier;
		if (m_3D && ((event->button() == Qt::RightButton && !shift) || (event->button() == Qt::LeftButton && alt)))
		{
			m_Orbiting = true;
		}
		else if (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton)
		{
			m_Panning = true;
		}
		else if (event->button() == Qt::LeftButton)
		{
			const Vec2 pixel = ToPixels(event->position());
			const Entity picked = Pick(pixel);
			m_Selected = picked;
			emit EntityPicked(picked);
			if (picked != NullEntity)
			{
				// Grab on a plane facing the camera through the object.
				const RenderView view = CurrentView();
				const Vec3 position = m_World->Get<TransformComponent>(picked).Position;
				m_DragPlanePoint = position;
				m_DragPlaneNormal = -view.CameraForward;
				const Ray ray = view.ScreenToRay(pixel);
				const float denom = Vec3::Dot(ray.Direction, m_DragPlaneNormal);
				if (std::fabs(denom) > 1e-6f)
				{
					const float t = Vec3::Dot(m_DragPlanePoint - ray.Origin, m_DragPlaneNormal) / denom;
					m_DragOffset = position - (ray.Origin + ray.Direction * t);
					m_Dragging = true;
				}
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
		m_Orbiting = false;
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

		const QPointF delta = position - m_LastMouse;
		const float dx = static_cast<float>(delta.x());
		const float dy = static_cast<float>(delta.y());
		if (m_Orbiting)
		{
			m_Yaw -= dx * 0.3f;
			m_Pitch = std::clamp(m_Pitch - dy * 0.3f, -89.0f, 89.0f);
		}
		else if (m_Panning)
		{
			if (m_3D)
			{
				const RenderView view = CurrentView();
				const float scale = view.WorldPerPixel(m_OrbitTarget) * static_cast<float>(devicePixelRatioF());
				m_OrbitTarget += (view.CameraUp * dy - view.CameraRight * dx) * scale;
			}
			else
			{
				m_CameraCenter -= Vec2(dx, -dy) / m_PixelsPerUnit;
			}
		}
		else if (m_Dragging && m_World && m_World->IsValid(m_Selected))
		{
			const Ray ray = CurrentView().ScreenToRay(ToPixels(position));
			const float denom = Vec3::Dot(ray.Direction, m_DragPlaneNormal);
			if (std::fabs(denom) > 1e-6f)
			{
				const float t = Vec3::Dot(m_DragPlanePoint - ray.Origin, m_DragPlaneNormal) / denom;
				Vec3 target = ray.Origin + ray.Direction * t + m_DragOffset;
				// Hold shift to snap to a quarter unit grid. Axes facing the camera are left alone.
				if (event->modifiers() & Qt::ShiftModifier)
				{
					auto snap = [](float value) { return std::round(value * 4) / 4; };
					if (std::fabs(m_DragPlaneNormal.X) < 0.9f) target.X = snap(target.X);
					if (std::fabs(m_DragPlaneNormal.Y) < 0.9f) target.Y = snap(target.Y);
					if (std::fabs(m_DragPlaneNormal.Z) < 0.9f) target.Z = snap(target.Z);
				}
				m_World->Get<TransformComponent>(m_Selected).Position = target;
				emit EntityMoved(m_Selected);
			}
		}
		m_LastMouse = position;
	}

	void ViewportWidget::wheelEvent(QWheelEvent* event)
	{
		if (m_Playing)
			return;
		const float steps = static_cast<float>(event->angleDelta().y());
		if (m_3D)
		{
			m_Distance = std::clamp(m_Distance * std::pow(0.998f, steps), 0.5f, 2000.0f);
			return;
		}
		// Zoom around the cursor so the point under it stays put.
		const Vec2 pixel = ToPixels(event->position());
		const Vec2 before = PixelToWorld2D(pixel);
		m_PixelsPerUnit = std::clamp(m_PixelsPerUnit * std::pow(1.0015f, steps), 4.0f, 1024.0f);
		const Vec2 after = PixelToWorld2D(pixel);
		m_CameraCenter += before - after;
	}

	void ViewportWidget::keyPressEvent(QKeyEvent* event)
	{
		if (m_Playing && !event->isAutoRepeat())
			Input::SetKey(ToScancode(event->key()), true);
		else if (!m_Playing && event->key() == Qt::Key_F)
			FocusSelected();
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
