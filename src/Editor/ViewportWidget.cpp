#include "Editor/ViewportWidget.h"
#include "Editor/PlaceEntitiesPanel.h"
#include "Engine/Input.h"

#include <SDL3/SDL.h>

#include <QCursor>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <cmath>

#ifdef __APPLE__
#include <CoreGraphics/CoreGraphics.h>
#endif

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

		// Moves the pointer without making a mouse event.
		void WarpCursor(QPoint global)
		{
#ifdef __APPLE__
			// Qt posts a fake event for this, which macOS can block. A plain warp works, and
			// unhooking the mouse around it skips the short freeze macOS adds after one.
			CGAssociateMouseAndMouseCursorPosition(false);
			CGWarpMouseCursorPosition(CGPointMake(global.x(), global.y()));
			CGAssociateMouseAndMouseCursorPosition(true);
#else
			QCursor::setPos(global);
#endif
		}

		// Bits in m_FlyKeys for the keys that fly the camera.
		enum FlyKey : uint32
		{
			FlyForward = 1 << 0,
			FlyBack = 1 << 1,
			FlyLeft = 1 << 2,
			FlyRight = 1 << 3,
			FlyDown = 1 << 4,
			FlyUp = 1 << 5,
		};

		uint32 ToFlyKey(int qtKey)
		{
			switch (qtKey)
			{
				case Qt::Key_W: return FlyForward;
				case Qt::Key_S: return FlyBack;
				case Qt::Key_A: return FlyLeft;
				case Qt::Key_D: return FlyRight;
				case Qt::Key_Q: return FlyDown;
				case Qt::Key_E: return FlyUp;
				default: return 0;
			}
		}
	}

	ViewportWidget::ViewportWidget(GPUDevice& device, QWidget* parent) : QWidget(parent), m_Device(device)
	{
		setFocusPolicy(Qt::StrongFocus);
		setMouseTracking(true);
		setMinimumSize(320, 240);
		setAttribute(Qt::WA_OpaquePaintEvent);
		setAcceptDrops(true);

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
		m_Flying = false;
		m_FlyKeys = 0;
		m_Gizmo.EndDrag();
		m_CursorFreed = false;
		Input::Reset();
		if (playing)
			setFocus();
		UpdateCursor();
	}

	void ViewportWidget::UpdateCursor()
	{
		const CursorMode mode = Input::GetCursorMode();
		const bool active = m_Playing && hasFocus() && isActiveWindow() && !m_CursorFreed;
		const bool hide = active && mode != CursorMode::Visible;
		const bool lock = active && mode == CursorMode::Locked;

		if (hide != m_CursorHidden)
		{
			if (hide)
				setCursor(Qt::BlankCursor);
			else
				unsetCursor();
			m_CursorHidden = hide;
		}
		if (lock != m_CursorLocked)
		{
			// Every click goes to the game while locked, even if the pointer slips off the viewport.
			if (lock)
				grabMouse();
			else
				releaseMouse();
		}
		if (lock)
		{
			const QPoint center = rect().center();
			const QPoint global = mapToGlobal(center);
			if (m_CursorLocked)
			{
				const QPoint moved = QCursor::pos() - global;
				Input::AddMouseDelta(Vec2(static_cast<float>(moved.x()), static_cast<float>(moved.y())) * static_cast<float>(devicePixelRatioF()));
			}
			WarpCursor(global);
			Input::SetMousePosition(ToPixels(QPointF(center)));
		}
		m_CursorLocked = lock;
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
			game.PixelScale = static_cast<float>(devicePixelRatioF());
			return game;
		}

		RenderView view;
		if (m_3D)
		{
			const TransformComponent transform = EditorCamera();
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

	TransformComponent ViewportWidget::EditorCamera() const
	{
		TransformComponent transform;
		transform.Rotation = { m_Pitch, m_Yaw, 0 };
		transform.Position = m_OrbitTarget - transform.GetForward() * m_Distance;
		return transform;
	}

	void ViewportWidget::SetGizmoMode(GizmoMode mode)
	{
		m_Gizmo.SetMode(mode);
		m_Gizmo.SetHover(TransformGizmo::Handle::None);
		update();
	}

	std::optional<TransformComponent> ViewportWidget::SelectedTransform() const
	{
		// The scene entity has a transform but no place in the world, so it gets no gizmo.
		if (!m_World || !m_World->IsValid(m_Selected) || !m_World->Has<TransformComponent>(m_Selected) || m_World->Has<SceneComponent>(m_Selected))
			return std::nullopt;
		return m_World->GetWorldTransform(m_Selected);
	}

	void ViewportWidget::Fly(float deltaTime)
	{
		if (!m_Flying || m_FlyKeys == 0)
			return;
		auto axis = [this](uint32 positive, uint32 negative) {
			return static_cast<float>((m_FlyKeys & positive) ? 1 : 0) - static_cast<float>((m_FlyKeys & negative) ? 1 : 0);
		};
		const float forward = axis(FlyForward, FlyBack);
		const float right = axis(FlyRight, FlyLeft);
		const float up = axis(FlyUp, FlyDown);
		const float speed = m_FlySpeed * (m_FlyFast ? 3.0f : 1.0f) * deltaTime;
		if (m_3D)
		{
			const TransformComponent camera = EditorCamera();
			// Q and E go straight down and up, like UE.
			m_OrbitTarget += (camera.GetForward() * forward + camera.GetRight() * right + Vec3(0, up, 0)) * speed;
		}
		else
		{
			// Same screen speed at any zoom. Q and E zoom out and in.
			m_CameraCenter += Vec2(right, forward) * speed * (48.0f / m_PixelsPerUnit);
			if (up != 0)
				m_PixelsPerUnit = std::clamp(m_PixelsPerUnit * std::pow(1.5f, up * deltaTime * 2), 4.0f, 1024.0f);
		}
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
		const Vec3 position = m_World->GetWorldPosition(m_Selected);
		m_CameraCenter = position.XY();
		m_OrbitTarget = position;
	}

	void ViewportWidget::AddGizmos(const RenderView& view)
	{
		const float dpr = static_cast<float>(devicePixelRatioF());

		for (auto [entity, transform, light] : m_World->View<TransformComponent, PointLightComponent>().each())
		{
			const Color color{ light.Color.R, light.Color.G, light.Color.B, 0.9f };
			const Vec3 p = m_World->GetWorldPosition(entity);
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
			const TransformComponent placed = m_World->GetWorldTransform(entity);
			const Vec3 p = placed.Position;
			const float handle = 10.0f * dpr * view.WorldPerPixel(p);
			m_Lines.AddCircle(p, handle, color, 16, view.CameraRight, view.CameraUp);
			const Vec3 forward = placed.GetForward();
			const Vec3 tip = p + forward * handle * 4.0f;
			m_Lines.AddLine(p, tip, color);
			m_Lines.AddLine(tip, tip - (forward + placed.GetRight() * 0.5f) * handle, color);
			m_Lines.AddLine(tip, tip - (forward - placed.GetRight() * 0.5f) * handle, color);
		}

		for (auto [entity, transform, camera] : m_World->View<TransformComponent, CameraComponent>().each())
		{
			const Color color{ 0.8f, 0.8f, 0.8f, 0.7f };
			const TransformComponent placed = m_World->GetWorldTransform(entity);
			const Vec3 p = placed.Position;
			const float handle = 10.0f * dpr * view.WorldPerPixel(p);
			const Mat4 rotation = Mat4::Translation(p) * Mat4::Rotation(placed.GetRotation());
			m_Lines.AddBox(rotation, Vec3(-1, -0.7f, -0.5f) * handle, Vec3(1, 0.7f, 0.5f) * handle, color);
			m_Lines.AddLine(p, p + placed.GetForward() * handle * 3.0f, color);
			// An orthographic camera also shows what it will see at the default 1280x720 window.
			if (!camera.Perspective && camera.Zoom > 0.01f)
				m_Lines.AddRect(rotation, { 1280.0f / camera.Zoom, 720.0f / camera.Zoom }, { color.R, color.G, color.B, 0.25f });
		}
	}

	void ViewportWidget::AddColliders()
	{
		for (auto [entity, collider] : m_World->View<ColliderComponent>().each())
		{
			// Triggers are amber so they read apart from solid colliders.
			const bool selected = entity == m_Selected;
			const Color base = collider.Trigger ? Color{ 1, 0.7f, 0.2f, 1 } : Color{ 0.3f, 1, 0.45f, 1 };
			const Color color{ base.R, base.G, base.B, selected ? 0.95f : 0.35f };

			// Shapes follow position and rotation, and scale changes their size like physics does.
			const TransformComponent placed = m_World->GetWorldTransform(entity);
			const Quat rotation = m_World->GetWorldRotation(entity);
			const Vec3 scale{ std::fabs(placed.Scale.X), std::fabs(placed.Scale.Y), std::fabs(placed.Scale.Z) };
			const Vec3 center = placed.Position + rotation.Rotate({ collider.Offset.X * scale.X, collider.Offset.Y * scale.Y, collider.Offset.Z * scale.Z });
			const Vec3 right = rotation.Rotate({ 1, 0, 0 });
			const Vec3 up = rotation.Rotate({ 0, 1, 0 });
			const Vec3 forward = rotation.Rotate({ 0, 0, 1 });
			const Mat4 frame = Mat4::Translation(center) * Mat4::Rotation(rotation);

			if (collider.Shape == "Sphere")
			{
				const float radius = collider.Radius * std::fmax(scale.X, std::fmax(scale.Y, scale.Z));
				m_Lines.AddCircle(center, radius, color, 32, right, up);
				m_Lines.AddCircle(center, radius, color, 32, right, forward);
				m_Lines.AddCircle(center, radius, color, 32, up, forward);
			}
			else if (collider.Shape == "Capsule")
			{
				const float radius = collider.Radius * std::fmax(scale.X, scale.Z);
				const float half = std::fmax(collider.Height * scale.Y * 0.5f - radius, 0.0f);
				const Vec3 top = center + up * half;
				const Vec3 bottom = center - up * half;
				m_Lines.AddCircle(top, radius, color, 24, right, forward);
				m_Lines.AddCircle(bottom, radius, color, 24, right, forward);
				for (const Vec3 side : { right, right * -1.0f, forward, forward * -1.0f })
					m_Lines.AddLine(top + side * radius, bottom + side * radius, color);
				// Full circles at the ends stand in for the rounded caps.
				for (const Vec3 end : { top, bottom })
				{
					m_Lines.AddCircle(end, radius, color, 24, right, up);
					m_Lines.AddCircle(end, radius, color, 24, forward, up);
				}
			}
			else if (collider.Shape == "Mesh")
			{
				// The mesh's bounds, since drawing every triangle would bury the scene.
				if (const GPUMesh* mesh = FindMesh(entity))
					m_Lines.AddBox(frame * Mat4::Scale(scale), mesh->BoundsMin, mesh->BoundsMax, color);
			}
			else
			{
				const Vec3 half{ collider.Size.X * scale.X * 0.5f, collider.Size.Y * scale.Y * 0.5f, collider.Size.Z * scale.Z * 0.5f };
				m_Lines.AddBox(frame, half * -1.0f, half, color);
			}
		}
	}

	void ViewportWidget::AddSelection(const RenderView& view)
	{
		if (!m_World->IsValid(m_Selected) || !m_World->Has<TransformComponent>(m_Selected))
			return;
		const Color color{ 1, 0.85f, 0.1f, 1 };
		const TransformComponent transform = m_World->GetWorldTransform(m_Selected);
		const Mat4 model = m_World->GetWorldMatrix(m_Selected);

		{
			if (const GPUMesh* gpuMesh = FindMesh(m_Selected))
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
		auto consider = [&](Entity entity, const TransformComponent&) {
			// Skip things behind a perspective camera.
			const Vec3 position = m_World->GetWorldPosition(entity);
			if (!view.Orthographic && Vec3::Dot(position - view.CameraPosition, view.CameraForward) <= 0)
				return;
			const float distance = (view.WorldToScreen(position) - pixel).Length();
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

	const GPUMesh* ViewportWidget::FindMesh(Entity entity) const
	{
		if (const MeshComponent* mesh = m_World->TryGet<MeshComponent>(entity))
			return m_Renderer->GetMeshes().Get(mesh->Mesh);
		if (const SkeletalMeshComponent* skeletal = m_World->TryGet<SkeletalMeshComponent>(entity))
			return m_Renderer->GetMeshes().GetSkinned(skeletal->Mesh);
		return nullptr;
	}

	bool ViewportWidget::HitSurface(const Ray& ray, Entity entity, float& outDistance) const
	{
		const SpriteComponent* sprite = m_World->TryGet<SpriteComponent>(entity);

		Vec3 min{ -0.5f, -0.5f, 0 };
		Vec3 max{ 0.5f, 0.5f, 0 };
		Mat4 model = m_World->GetWorldMatrix(entity);
		if (sprite)
		{
			model = model * Mat4::Scale({ sprite->Size, 1 });
		}
		else if (const GPUMesh* gpuMesh = FindMesh(entity))
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
		for (auto [entity, transform, mesh] : m_World->View<TransformComponent, SkeletalMeshComponent>().each())
			consider(entity, 0);
		return best;
	}

	void ViewportWidget::RenderFrame()
	{
		RecreateTarget();
		if (!m_Renderer->IsValid() || !m_World || !m_Target)
			return;

		// Frame time for flying. Stalls are capped so the camera never jumps.
		const uint64 now = SDL_GetTicksNS();
		const float deltaTime = m_LastFrameTicks ? std::fmin(static_cast<float>(now - m_LastFrameTicks) / 1e9f, 0.1f) : 0.0f;
		m_LastFrameTicks = now;
		if (!m_Playing)
			Fly(deltaTime);

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
			AddColliders();
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
		// Drawn with Qt over the frame so handles are thick, smooth and always on top.
		if (!m_Playing && m_Target)
		{
			if (const std::optional<TransformComponent> transform = SelectedTransform())
				m_Gizmo.Paint(painter, CurrentView(), static_cast<float>(devicePixelRatioF()), *transform);
		}
	}

	void ViewportWidget::resizeEvent(QResizeEvent*)
	{
		if (m_Overlay)
			m_Overlay->setGeometry(0, 0, width(), m_Overlay->sizeHint().height());
		RenderFrame();
	}

	void ViewportWidget::SetOverlay(QWidget* overlay)
	{
		m_Overlay = overlay;
		overlay->setParent(this);
		overlay->setGeometry(0, 0, width(), overlay->sizeHint().height());
		overlay->show();
	}

	void ViewportWidget::mousePressEvent(QMouseEvent* event)
	{
		setFocus();
		m_LastMouse = event->position();
		if (m_Playing)
		{
			// The click that takes the mouse back is not a game click.
			if (m_CursorFreed && Input::GetCursorMode() != CursorMode::Visible)
			{
				m_CursorFreed = false;
				UpdateCursor();
				return;
			}
			if (event->button() == Qt::LeftButton) Input::SetMouseButton(MouseButton::Left, true);
			if (event->button() == Qt::RightButton) Input::SetMouseButton(MouseButton::Right, true);
			if (event->button() == Qt::MiddleButton) Input::SetMouseButton(MouseButton::Middle, true);
			return;
		}
		if (!m_World)
			return;

		const bool alt = event->modifiers() & Qt::AltModifier;
		const bool shift = event->modifiers() & Qt::ShiftModifier;
		if (event->button() == Qt::RightButton && !shift)
		{
			// Looks around in 3D and pans in 2D. WASD flies while it is held.
			m_Flying = true;
			m_FlyKeys = 0;
			if (m_3D)
				m_Looking = true;
			else
				m_Panning = true;
		}
		else if (m_3D && event->button() == Qt::LeftButton && alt)
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
			const RenderView view = CurrentView();
			const float dpr = static_cast<float>(devicePixelRatioF());
			// Handles of the selected entity's gizmo come before anything they cover.
			if (const std::optional<TransformComponent> transform = SelectedTransform())
			{
				if (m_Gizmo.BeginDrag(m_Gizmo.HitTest(view, dpr, *transform, pixel), view, dpr, *transform, pixel))
					return;
			}

			const Entity picked = Pick(pixel);
			m_Selected = picked;
			m_Gizmo.SetHover(TransformGizmo::Handle::None);
			emit EntityPicked(picked);
			// Dragging the entity itself moves it, unless the gizmo is set to rotate or scale.
			const bool freeMove = m_Gizmo.GetMode() == GizmoMode::Select || m_Gizmo.GetMode() == GizmoMode::Move;
			if (picked != NullEntity && freeMove)
			{
				// Grab on a plane facing the camera through the object.
				const Vec3 position = m_World->GetWorldPosition(picked);
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
		if (event->button() == Qt::RightButton)
		{
			m_Flying = false;
			m_FlyKeys = 0;
			m_Looking = false;
		}
		m_Panning = false;
		m_Orbiting = false;
		m_Dragging = false;
		m_Gizmo.EndDrag();
		update();
	}

	void ViewportWidget::mouseMoveEvent(QMouseEvent* event)
	{
		const QPointF position = event->position();
		if (m_Playing)
		{
			// UpdateCursor keeps a locked mouse at the center.
			if (!m_CursorLocked)
				Input::SetMousePosition(ToPixels(position));
			return;
		}

		const QPointF delta = position - m_LastMouse;
		const float dx = static_cast<float>(delta.x());
		const float dy = static_cast<float>(delta.y());
		const bool shift = event->modifiers() & Qt::ShiftModifier;
		if (m_Gizmo.IsDragging())
		{
			if (std::optional<TransformComponent> transform = SelectedTransform())
			{
				// Snapping is on with the toolbar button, or off with Shift held.
				// The gizmo works in world space, so the result goes back through the parents.
				const bool snapping = m_SnapEnabled != shift;
				m_Gizmo.UpdateDrag(CurrentView(), ToPixels(position), *transform, snapping ? m_SnapSize : 0.0f, snapping);
				m_World->SetWorldTransform(m_Selected, *transform);
				emit EntityMoved(m_Selected);
				update();
			}
		}
		else if (m_Looking)
		{
			// Turn in place by keeping the camera where it is and moving the target around it.
			const Vec3 eye = EditorCamera().Position;
			m_Yaw -= dx * 0.3f;
			m_Pitch = std::clamp(m_Pitch - dy * 0.3f, -89.0f, 89.0f);
			TransformComponent turned;
			turned.Rotation = { m_Pitch, m_Yaw, 0 };
			m_OrbitTarget = eye + turned.GetForward() * m_Distance;
		}
		else if (m_Orbiting)
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
				// Snap to the grid when snapping is on, or off with Shift held. Axes facing the camera are left alone.
				if (m_SnapEnabled != shift && m_SnapSize > 0)
				{
					auto snap = [this](float value) { return std::round(value / m_SnapSize) * m_SnapSize; };
					if (std::fabs(m_DragPlaneNormal.X) < 0.9f) target.X = snap(target.X);
					if (std::fabs(m_DragPlaneNormal.Y) < 0.9f) target.Y = snap(target.Y);
					if (std::fabs(m_DragPlaneNormal.Z) < 0.9f) target.Z = snap(target.Z);
				}
				m_World->SetWorldPosition(m_Selected, target);
				emit EntityMoved(m_Selected);
			}
		}
		else if (event->buttons() == Qt::NoButton)
		{
			// Light up the handle under the mouse.
			const std::optional<TransformComponent> transform = SelectedTransform();
			const TransformGizmo::Handle hover = transform
				? m_Gizmo.HitTest(CurrentView(), static_cast<float>(devicePixelRatioF()), *transform, ToPixels(position))
				: TransformGizmo::Handle::None;
			m_Gizmo.SetHover(hover);
			update();
		}
		m_LastMouse = position;
	}

	void ViewportWidget::wheelEvent(QWheelEvent* event)
	{
		if (m_Playing)
		{
			// One notch is 120, the same as one SDL wheel step.
			Input::AddScroll(Vec2(static_cast<float>(event->angleDelta().x()), static_cast<float>(event->angleDelta().y())) / 120.0f);
			return;
		}
		const float steps = static_cast<float>(event->angleDelta().y());
		if (m_Flying)
		{
			// While flying the wheel sets the speed, like UE.
			m_FlySpeed = std::clamp(m_FlySpeed * std::pow(1.2f, steps / 120.0f), 0.25f, 500.0f);
			return;
		}
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
		if (m_Playing)
		{
			// Escape frees the mouse so the editor can be used. The game still sees the key.
			if (event->key() == Qt::Key_Escape)
			{
				m_CursorFreed = true;
				UpdateCursor();
			}
			if (!event->isAutoRepeat())
				Input::SetKey(ToScancode(event->key()), true);
			// Printable text goes to UI text boxes. Control characters arrive as key presses instead.
			const QString text = event->text();
			if (!text.isEmpty() && text[0].isPrint())
				Input::AddTypedText(text.toUtf8().constData());
			return;
		}
		m_FlyFast = event->modifiers() & Qt::ShiftModifier;
		if (m_Flying && ToFlyKey(event->key()))
		{
			m_FlyKeys |= ToFlyKey(event->key());
			return;
		}
		// Gizmo keys only without modifiers, so Ctrl+S and friends still reach the menus.
		const bool plain = (event->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier;
		const GizmoMode modes[] = { GizmoMode::Select, GizmoMode::Move, GizmoMode::Rotate, GizmoMode::Scale };
		const int keys[] = { Qt::Key_Q, Qt::Key_W, Qt::Key_E, Qt::Key_R };
		for (int i = 0; i < 4; ++i)
		{
			if (plain && event->key() == keys[i] && !event->isAutoRepeat())
			{
				SetGizmoMode(modes[i]);
				emit GizmoModeChanged(modes[i]);
				return;
			}
		}
		if (event->key() == Qt::Key_F)
			FocusSelected();
		else
			QWidget::keyPressEvent(event);
	}

	void ViewportWidget::keyReleaseEvent(QKeyEvent* event)
	{
		if (m_Playing)
		{
			if (!event->isAutoRepeat())
				Input::SetKey(ToScancode(event->key()), false);
			return;
		}
		m_FlyFast = event->modifiers() & Qt::ShiftModifier;
		if (!event->isAutoRepeat() && ToFlyKey(event->key()))
			m_FlyKeys &= ~ToFlyKey(event->key());
		else
			QWidget::keyReleaseEvent(event);
	}

	void ViewportWidget::dragEnterEvent(QDragEnterEvent* event)
	{
		if (!m_Playing && (event->mimeData()->hasUrls() || event->mimeData()->hasFormat(PlaceEntitiesPanel::MimeType)))
			event->acceptProposedAction();
	}

	void ViewportWidget::dropEvent(QDropEvent* event)
	{
		if (m_Playing || !m_World)
			return;
		const Vec2 pixel = ToPixels(event->position());
		const Ray ray = CurrentView().ScreenToRay(pixel);

		// 2D drops land on the sprite plane, 3D drops on the ground.
		Vec3 point = ray.Origin;
		if (m_3D)
		{
			if (std::fabs(ray.Direction.Y) > 1e-6f)
			{
				const float t = -ray.Origin.Y / ray.Direction.Y;
				if (t > 0)
					point = ray.Origin + ray.Direction * t;
			}
		}
		else
		{
			ray.IntersectZPlane(0, point);
		}

		if (event->mimeData()->hasFormat(PlaceEntitiesPanel::MimeType))
		{
			emit EntityDropped(QString::fromUtf8(event->mimeData()->data(PlaceEntitiesPanel::MimeType)), point);
			event->acceptProposedAction();
			return;
		}

		const Entity target = Pick(pixel);
		for (const QUrl& url : event->mimeData()->urls())
		{
			if (url.isLocalFile())
				emit AssetDropped(url.toLocalFile(), point, target);
		}
		event->acceptProposedAction();
	}

	void ViewportWidget::focusOutEvent(QFocusEvent* event)
	{
		Input::Reset();
		m_FlyKeys = 0;
		UpdateCursor();
		QWidget::focusOutEvent(event);
	}
}
