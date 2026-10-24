#include "Editor/TransformGizmo.h"

#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace FP::Editor
{
	namespace
	{
		// Sizes in logical pixels so the gizmo looks the same on any screen.
		constexpr float ArrowLength = 115;
		constexpr float HeadLength = 16;
		constexpr float HeadWidth = 11;
		constexpr float BoxSize = 10;
		constexpr float CenterRadius = 7;
		constexpr float HitTolerance = 8;
		constexpr float PlaneStart = 0.22f;
		constexpr float PlaneEnd = 0.42f;
		constexpr int RingSegments = 64;
		constexpr float AngleStep = 15;

		const QColor AxisColors[3] = { QColor(232, 52, 52), QColor(98, 206, 50), QColor(46, 112, 255) };
		const QColor Highlight(255, 214, 20);

		float& At(Vec3& v, int axis) { return axis == 0 ? v.X : axis == 1 ? v.Y : v.Z; }
		float At(const Vec3& v, int axis) { return axis == 0 ? v.X : axis == 1 ? v.Y : v.Z; }

		Vec3 Unit(int axis) { return axis == 0 ? Vec3(1, 0, 0) : axis == 1 ? Vec3(0, 1, 0) : Vec3(0, 0, 1); }

		TransformGizmo::Handle AxisHandle(int axis)
		{
			return static_cast<TransformGizmo::Handle>(static_cast<int>(TransformGizmo::Handle::X) + axis);
		}

		// Plane handles by the axis they leave out: YZ, XZ, XY.
		TransformGizmo::Handle PlaneHandle(int normal)
		{
			return normal == 0 ? TransformGizmo::Handle::YZ : normal == 1 ? TransformGizmo::Handle::XZ : TransformGizmo::Handle::XY;
		}

		int AxisOf(TransformGizmo::Handle handle)
		{
			switch (handle)
			{
				case TransformGizmo::Handle::X: case TransformGizmo::Handle::YZ: return 0;
				case TransformGizmo::Handle::Y: case TransformGizmo::Handle::XZ: return 1;
				default: return 2;
			}
		}

		bool IsPlane(TransformGizmo::Handle handle)
		{
			return handle == TransformGizmo::Handle::XY || handle == TransformGizmo::Handle::YZ || handle == TransformGizmo::Handle::XZ;
		}

		QPointF ToLogical(Vec2 pixel, float dpr) { return { pixel.X / dpr, pixel.Y / dpr }; }

		float DistanceToSegment(Vec2 p, Vec2 a, Vec2 b)
		{
			const Vec2 ab = b - a;
			const float lengthSquared = ab.LengthSquared();
			const float t = lengthSquared > 1e-6f ? std::clamp(((p.X - a.X) * ab.X + (p.Y - a.Y) * ab.Y) / lengthSquared, 0.0f, 1.0f) : 0.0f;
			return (p - (a + ab * t)).Length();
		}

		// Distance along the axis line to the point closest to the ray. False when they are parallel.
		bool AxisParam(const Ray& ray, Vec3 origin, Vec3 axis, float& out)
		{
			const Vec3 w = origin - ray.Origin;
			const float b = Vec3::Dot(axis, ray.Direction);
			const float c = Vec3::Dot(ray.Direction, ray.Direction);
			const float denom = c - b * b;
			if (denom < 1e-6f)
				return false;
			out = (b * Vec3::Dot(ray.Direction, w) - c * Vec3::Dot(axis, w)) / denom;
			return true;
		}

		bool IntersectPlane(const Ray& ray, Vec3 point, Vec3 normal, Vec3& out)
		{
			const float denom = Vec3::Dot(ray.Direction, normal);
			if (std::fabs(denom) < 1e-6f)
				return false;
			out = ray.Origin + ray.Direction * (Vec3::Dot(point - ray.Origin, normal) / denom);
			return true;
		}

		float Snap(float value, float step) { return std::round(value / step) * step; }

		// The square for a plane handle, spanned by the two axes other than normal.
		QPolygonF PlaneQuad(const RenderView& view, Vec3 origin, Vec3 a, Vec3 b, float length, float dpr)
		{
			QPolygonF quad;
			for (const auto& [s, t] : { std::pair{ PlaneStart, PlaneStart }, std::pair{ PlaneEnd, PlaneStart }, std::pair{ PlaneEnd, PlaneEnd }, std::pair{ PlaneStart, PlaneEnd } })
				quad.append(ToLogical(view.WorldToScreen(origin + (a * s + b * t) * length), dpr));
			return quad;
		}
	}

	TransformGizmo::Frame TransformGizmo::MakeFrame(const RenderView& view, float dpr, const TransformComponent& transform, GizmoMode mode) const
	{
		Frame frame;
		frame.Origin = transform.Position;
		const bool local = m_Local || mode == GizmoMode::Scale;
		const Quat rotation = transform.GetRotation();
		for (int axis = 0; axis < 3; ++axis)
			frame.Axes[axis] = local ? rotation.Rotate(Unit(axis)) : Unit(axis);
		frame.Length = ArrowLength * dpr * view.WorldPerPixel(frame.Origin);
		return frame;
	}

	bool TransformGizmo::ShowsAxis(const RenderView& view, const Frame& frame, int axis) const
	{
		if (!m_3D)
			return m_Mode == GizmoMode::Rotate ? axis == 2 : axis != 2;
		if (m_Mode == GizmoMode::Rotate)
			return true;
		// An arrow pointing at the camera is a dot that cannot be dragged sensibly.
		const Vec2 origin = view.WorldToScreen(frame.Origin);
		const Vec2 tip = view.WorldToScreen(frame.Origin + frame.Axes[axis] * frame.Length);
		const float full = frame.Length / view.WorldPerPixel(frame.Origin);
		return (tip - origin).Length() > full * 0.15f;
	}

	bool TransformGizmo::IsVisible(const RenderView& view, const TransformComponent& transform) const
	{
		if (m_Mode == GizmoMode::Select)
			return false;
		return view.Orthographic || Vec3::Dot(transform.Position - view.CameraPosition, view.CameraForward) > 0;
	}

	void TransformGizmo::Paint(QPainter& painter, const RenderView& view, float dpr, const TransformComponent& transform) const
	{
		if (!IsVisible(view, transform))
			return;
		painter.save();
		painter.setRenderHint(QPainter::Antialiasing);
		const Frame frame = IsDragging() ? m_StartFrame : MakeFrame(view, dpr, transform, m_Mode);
		const Frame shown = MakeFrame(view, dpr, transform, m_Mode);
		const Vec2 originPixel = view.WorldToScreen(shown.Origin);
		const QPointF origin = ToLogical(originPixel, dpr);
		const Handle lit = IsDragging() ? m_Active : m_Hover;
		auto colorFor = [&](Handle handle, QColor base) { return handle == lit ? Highlight : base; };
		// Rotating shows the axes the drag started with so the ring does not spin under the mouse.
		const Vec3* axes = m_Mode == GizmoMode::Rotate && IsDragging() ? frame.Axes : shown.Axes;

		if (m_Mode == GizmoMode::Rotate)
		{
			for (int axis = 0; axis < 3; ++axis)
			{
				if (!ShowsAxis(view, shown, axis))
					continue;
				const QColor color = colorFor(AxisHandle(axis), AxisColors[axis]);
				const Vec3 u = axes[(axis + 1) % 3];
				const Vec3 v = axes[(axis + 2) % 3];
				Vec3 previous = shown.Origin + u * shown.Length;
				for (int i = 1; i <= RingSegments; ++i)
				{
					const float angle = 2 * Pi * static_cast<float>(i) / RingSegments;
					const Vec3 point = shown.Origin + (u * std::cos(angle) + v * std::sin(angle)) * shown.Length;
					// The far half of a ring is faint so the rings read as a sphere in 3D.
					const bool back = m_3D && Vec3::Dot((point + previous) * 0.5f - shown.Origin, view.CameraForward) > 0;
					QColor segment = color;
					segment.setAlpha(back ? 70 : 255);
					painter.setPen(QPen(segment, back ? 2 : 3, Qt::SolidLine, Qt::RoundCap));
					painter.drawLine(ToLogical(view.WorldToScreen(previous), dpr), ToLogical(view.WorldToScreen(point), dpr));
					previous = point;
				}
			}
			painter.setPen(Qt::NoPen);
			painter.setBrush(QColor(255, 255, 255, 200));
			painter.drawEllipse(origin, 3, 3);
			painter.restore();
			return;
		}

		// Plane squares first so the arrows draw over them.
		if (m_Mode == GizmoMode::Move)
		{
			for (int normal = 0; normal < 3; ++normal)
			{
				const int a = (normal + 1) % 3;
				const int b = (normal + 2) % 3;
				if (!ShowsAxis(view, shown, a) || !ShowsAxis(view, shown, b))
					continue;
				QColor color = colorFor(PlaneHandle(normal), AxisColors[normal]);
				painter.setPen(QPen(color, 1.5));
				color.setAlpha(110);
				painter.setBrush(color);
				painter.drawPolygon(PlaneQuad(view, shown.Origin, axes[a], axes[b], shown.Length, dpr));
			}
		}

		for (int axis = 0; axis < 3; ++axis)
		{
			if (!ShowsAxis(view, shown, axis))
				continue;
			const QColor color = colorFor(AxisHandle(axis), AxisColors[axis]);
			const Vec2 tipPixel = view.WorldToScreen(shown.Origin + axes[axis] * shown.Length);
			const QPointF tip = ToLogical(tipPixel, dpr);
			const Vec2 direction = (tipPixel - originPixel).Normalized();
			const QPointF dir(direction.X, direction.Y);
			const QPointF side(-direction.Y, direction.X);
			painter.setPen(QPen(color, 3, Qt::SolidLine, Qt::FlatCap));
			painter.setBrush(color);
			if (m_Mode == GizmoMode::Move)
			{
				const QPointF base = tip - dir * HeadLength;
				painter.drawLine(origin, base);
				painter.setPen(Qt::NoPen);
				painter.drawPolygon(QPolygonF({ tip, base + side * HeadWidth * 0.5, base - side * HeadWidth * 0.5 }));
			}
			else
			{
				painter.drawLine(origin, tip);
				painter.setPen(Qt::NoPen);
				painter.drawRect(QRectF(tip - QPointF(BoxSize, BoxSize) * 0.5, QSizeF(BoxSize, BoxSize)));
			}
		}

		painter.setPen(QPen(QColor(0, 0, 0, 120), 1));
		painter.setBrush(colorFor(Handle::Center, QColor(235, 235, 235)));
		if (m_Mode == GizmoMode::Move)
			painter.drawEllipse(origin, CenterRadius * 0.75, CenterRadius * 0.75);
		else
			painter.drawRect(QRectF(origin - QPointF(BoxSize, BoxSize) * 0.6, QSizeF(BoxSize, BoxSize) * 1.2));
		painter.restore();
	}

	TransformGizmo::Handle TransformGizmo::HitTest(const RenderView& view, float dpr, const TransformComponent& transform, Vec2 pixel) const
	{
		if (!IsVisible(view, transform))
			return Handle::None;
		const Frame frame = MakeFrame(view, dpr, transform, m_Mode);
		const Vec2 origin = view.WorldToScreen(frame.Origin);
		const float tolerance = HitTolerance * dpr;
		Handle best = Handle::None;
		float bestDistance = tolerance;

		if (m_Mode == GizmoMode::Rotate)
		{
			for (int axis = 0; axis < 3; ++axis)
			{
				if (!ShowsAxis(view, frame, axis))
					continue;
				const Vec3 u = frame.Axes[(axis + 1) % 3];
				const Vec3 v = frame.Axes[(axis + 2) % 3];
				Vec3 previous = frame.Origin + u * frame.Length;
				for (int i = 1; i <= RingSegments; ++i)
				{
					const float angle = 2 * Pi * static_cast<float>(i) / RingSegments;
					const Vec3 point = frame.Origin + (u * std::cos(angle) + v * std::sin(angle)) * frame.Length;
					// Front halves win over the faint back halves they cross.
					const bool back = m_3D && Vec3::Dot((point + previous) * 0.5f - frame.Origin, view.CameraForward) > 0;
					const float distance = DistanceToSegment(pixel, view.WorldToScreen(previous), view.WorldToScreen(point)) + (back ? tolerance * 0.5f : 0);
					if (distance < bestDistance)
					{
						best = AxisHandle(axis);
						bestDistance = distance;
					}
					previous = point;
				}
			}
			return best;
		}

		if ((pixel - origin).Length() < (CenterRadius + 3) * dpr)
			return Handle::Center;

		if (m_Mode == GizmoMode::Move)
		{
			for (int normal = 0; normal < 3; ++normal)
			{
				const int a = (normal + 1) % 3;
				const int b = (normal + 2) % 3;
				if (ShowsAxis(view, frame, a) && ShowsAxis(view, frame, b)
					&& PlaneQuad(view, frame.Origin, frame.Axes[a], frame.Axes[b], frame.Length, dpr).containsPoint(ToLogical(pixel, dpr), Qt::OddEvenFill))
					return PlaneHandle(normal);
			}
		}

		for (int axis = 0; axis < 3; ++axis)
		{
			if (!ShowsAxis(view, frame, axis))
				continue;
			const float distance = DistanceToSegment(pixel, origin, view.WorldToScreen(frame.Origin + frame.Axes[axis] * frame.Length));
			if (distance < bestDistance)
			{
				best = AxisHandle(axis);
				bestDistance = distance;
			}
		}
		return best;
	}

	bool TransformGizmo::BeginDrag(Handle handle, const RenderView& view, float dpr, const TransformComponent& transform, Vec2 pixel)
	{
		if (handle == Handle::None)
			return false;
		m_StartFrame = MakeFrame(view, dpr, transform, m_Mode);
		m_Start = transform;
		m_StartPixel = pixel;
		m_LengthPixels = ArrowLength * dpr;
		m_Angle = 0;

		const Ray ray = view.ScreenToRay(pixel);
		const int axis = AxisOf(handle);
		bool ok = true;
		if (m_Mode == GizmoMode::Rotate)
		{
			const Vec2 origin = view.WorldToScreen(m_StartFrame.Origin);
			m_LastScreenAngle = std::atan2(pixel.Y - origin.Y, pixel.X - origin.X);
		}
		else if (handle == Handle::Center)
		{
			ok = m_Mode == GizmoMode::Scale || IntersectPlane(ray, m_StartFrame.Origin, view.CameraForward, m_StartHit);
		}
		else if (IsPlane(handle))
		{
			ok = IntersectPlane(ray, m_StartFrame.Origin, m_StartFrame.Axes[axis], m_StartHit);
		}
		else
		{
			ok = AxisParam(ray, m_StartFrame.Origin, m_StartFrame.Axes[axis], m_StartParam);
		}
		m_Active = ok ? handle : Handle::None;
		return ok;
	}

	void TransformGizmo::UpdateDrag(const RenderView& view, Vec2 pixel, TransformComponent& transform, float snap, bool snapAngles)
	{
		if (!IsDragging())
			return;
		const Frame& frame = m_StartFrame;
		const Ray ray = view.ScreenToRay(pixel);
		const int axis = AxisOf(m_Active);
		// Grid snapping lines up world coordinates. Along the entity's own axes it snaps the distance moved.
		const bool worldAxes = !m_Local;

		if (m_Mode == GizmoMode::Move)
		{
			Vec3 position = m_Start.Position;
			if (m_Active == Handle::Center || IsPlane(m_Active))
			{
				const Vec3 normal = m_Active == Handle::Center ? view.CameraForward : frame.Axes[axis];
				Vec3 hit;
				if (!IntersectPlane(ray, frame.Origin, normal, hit))
					return;
				Vec3 delta = hit - m_StartHit;
				if (snap > 0 && (!worldAxes && m_Active != Handle::Center))
				{
					// Snap the distance along each of the plane's two axes.
					const Vec3 a = frame.Axes[(axis + 1) % 3];
					const Vec3 b = frame.Axes[(axis + 2) % 3];
					delta = a * Snap(Vec3::Dot(delta, a), snap) + b * Snap(Vec3::Dot(delta, b), snap);
				}
				position = m_Start.Position + delta;
				if (snap > 0 && (worldAxes || m_Active == Handle::Center))
				{
					// Axes the plane faces stay where they were.
					for (int i = 0; i < 3; ++i)
					{
						if (std::fabs(At(normal, i)) < 0.9f)
							At(position, i) = Snap(At(position, i), snap);
					}
				}
			}
			else
			{
				float param = 0;
				if (!AxisParam(ray, frame.Origin, frame.Axes[axis], param))
					return;
				float distance = param - m_StartParam;
				if (snap > 0 && !worldAxes)
					distance = Snap(distance, snap);
				position = m_Start.Position + frame.Axes[axis] * distance;
				if (snap > 0 && worldAxes)
					At(position, axis) = Snap(At(position, axis), snap);
			}
			transform.Position = position;
		}
		else if (m_Mode == GizmoMode::Rotate)
		{
			// Follow the mouse around the gizmo's center on screen. Works from any angle, even edge on.
			const Vec2 origin = view.WorldToScreen(frame.Origin);
			const float screenAngle = std::atan2(pixel.Y - origin.Y, pixel.X - origin.X);
			float step = screenAngle - m_LastScreenAngle;
			if (step > Pi) step -= 2 * Pi;
			if (step < -Pi) step += 2 * Pi;
			m_LastScreenAngle = screenAngle;
			m_Angle += step;

			// Screen angles grow clockwise. A positive turn looks counterclockwise when the axis faces us.
			const bool facing = Vec3::Dot(frame.Axes[axis], view.CameraForward) < 0;
			float degrees = (facing ? -m_Angle : m_Angle) * RadToDeg;
			if (snapAngles)
				degrees = Snap(degrees, AngleStep);

			const Quat start = m_Start.GetRotation();
			Vec3 rotation = m_Start.Rotation;
			// Turns that map onto one Euler angle add to it, so values stay readable and never wrap.
			if (m_Local && axis == 2)
				rotation.Z += degrees;
			else if (!m_Local && axis == 1)
				rotation.Y += degrees;
			else if (!m_Local && axis == 2 && rotation.X == 0 && rotation.Y == 0)
				rotation.Z += degrees;
			else if (m_Local && axis == 1 && rotation.X == 0 && rotation.Z == 0)
				rotation.Y += degrees;
			else if (m_Local)
				rotation = (start * Quat::AxisAngle(Unit(axis), degrees)).ToEuler();
			else
				rotation = (Quat::AxisAngle(Unit(axis), degrees) * start).ToEuler();
			transform.Rotation = rotation;
		}
		else if (m_Mode == GizmoMode::Scale)
		{
			if (m_Active == Handle::Center)
			{
				// Right or up grows, left or down shrinks.
				const float moved = (pixel.X - m_StartPixel.X) - (pixel.Y - m_StartPixel.Y);
				const float factor = std::fmax(0.01f, 1 + moved / m_LengthPixels);
				transform.Scale = m_Start.Scale * factor;
			}
			else
			{
				float param = 0;
				if (!AxisParam(ray, frame.Origin, frame.Axes[axis], param))
					return;
				Vec3 scale = m_Start.Scale;
				At(scale, axis) = At(m_Start.Scale, axis) * (1 + (param - m_StartParam) / frame.Length);
				transform.Scale = scale;
			}
		}
	}
}
