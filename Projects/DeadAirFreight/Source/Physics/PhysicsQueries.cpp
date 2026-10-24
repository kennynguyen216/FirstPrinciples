#include "Physics/PhysicsQueries.h"

#include <algorithm>
#include <cmath>

using namespace FP;

namespace
{
	const Array<const char*> LayerNames = { "Static", "Truck", "Cargo", "Siphon", "Player", "Trigger" };
	const Array<const char*> ShapeNames = { "Box", "Sphere", "Capsule" };
	const Array<const char*> MotionNames = { "Static", "Kinematic", "Dynamic" };

	constexpr float Epsilon = 1e-6f;

	Quat Inverse(const Quat& q) { return { -q.X, -q.Y, -q.Z, q.W }; }

	Vec3 SafeDivide(Vec3 v, Vec3 by)
	{
		return { v.X / (std::abs(by.X) > Epsilon ? by.X : Epsilon), v.Y / (std::abs(by.Y) > Epsilon ? by.Y : Epsilon), v.Z / (std::abs(by.Z) > Epsilon ? by.Z : Epsilon) };
	}

	// Ray against a box centered on the origin. t is along the given (unnormalized) direction.
	bool RayBox(Vec3 origin, Vec3 direction, Vec3 half, float& outT, Vec3& outNormal)
	{
		float enter = -1e30f;
		float exit = 1e30f;
		Vec3 normal;
		const float o[3] = { origin.X, origin.Y, origin.Z };
		const float d[3] = { direction.X, direction.Y, direction.Z };
		const float h[3] = { half.X, half.Y, half.Z };
		for (int32 axis = 0; axis < 3; ++axis)
		{
			if (std::abs(d[axis]) < Epsilon)
			{
				if (o[axis] < -h[axis] || o[axis] > h[axis])
					return false;
				continue;
			}
			float near = (-h[axis] - o[axis]) / d[axis];
			float far = (h[axis] - o[axis]) / d[axis];
			float sign = -1;
			if (near > far)
			{
				std::swap(near, far);
				sign = 1;
			}
			if (near > enter)
			{
				enter = near;
				normal = {};
				(axis == 0 ? normal.X : axis == 1 ? normal.Y : normal.Z) = sign;
			}
			exit = std::min(exit, far);
			if (enter > exit)
				return false;
		}
		if (exit < 0)
			return false;
		outT = std::max(enter, 0.0f);
		outNormal = normal;
		return true;
	}

	bool RaySphere(Vec3 origin, Vec3 direction, Vec3 center, float radius, float& outT, Vec3& outNormal)
	{
		const Vec3 offset = origin - center;
		const float a = Vec3::Dot(direction, direction);
		const float b = Vec3::Dot(offset, direction);
		const float c = Vec3::Dot(offset, offset) - radius * radius;
		const float discriminant = b * b - a * c;
		if (a < Epsilon || discriminant < 0)
			return false;
		float t = (-b - std::sqrt(discriminant)) / a;
		if (t < 0)
			t = c < 0 ? 0 : (-b + std::sqrt(discriminant)) / a; // inside starts at 0
		if (t < 0)
			return false;
		outT = t;
		outNormal = (offset + direction * t).Normalized();
		return true;
	}

	// Capsule along local Y: the closest hit of its two end spheres and the cylinder between them.
	bool RayCapsule(Vec3 origin, Vec3 direction, float radius, float halfHeight, float& outT, Vec3& outNormal)
	{
		bool hit = false;
		float best = 1e30f;
		const float a = direction.X * direction.X + direction.Z * direction.Z;
		if (a > Epsilon)
		{
			const float b = origin.X * direction.X + origin.Z * direction.Z;
			const float c = origin.X * origin.X + origin.Z * origin.Z - radius * radius;
			const float discriminant = b * b - a * c;
			if (discriminant >= 0)
			{
				const float t = std::max((-b - std::sqrt(discriminant)) / a, 0.0f);
				const float y = origin.Y + direction.Y * t;
				if (std::abs(y) <= halfHeight && (-b + std::sqrt(discriminant)) / a >= 0)
				{
					best = t;
					outNormal = Vec3{ origin.X + direction.X * t, 0, origin.Z + direction.Z * t }.Normalized();
					hit = true;
				}
			}
		}
		for (float end : { -halfHeight, halfHeight })
		{
			float t = 0;
			Vec3 normal;
			if (RaySphere(origin, direction, { 0, end, 0 }, radius, t, normal) && t < best)
			{
				best = t;
				outNormal = normal;
				hit = true;
			}
		}
		outT = best;
		return hit;
	}

	float MaxComponent(Vec3 v) { return std::max({ std::abs(v.X), std::abs(v.Y), std::abs(v.Z) }); }
}

PhysicsLayer ParsePhysicsLayer(const String& name)
{
	for (int32 i = 0; i < LayerNames.Num(); ++i)
	{
		if (name == LayerNames[i])
			return static_cast<PhysicsLayer>(i);
	}
	return PhysicsLayer::Static;
}

const Array<const char*>& GetPhysicsLayerNames() { return LayerNames; }
const Array<const char*>& GetColliderShapeNames() { return ShapeNames; }
const Array<const char*>& GetMotionTypeNames() { return MotionNames; }

namespace PhysicsQueries
{
	bool Raycast(World& world, Vec3 origin, Vec3 direction, float maxDistance, PhysicsLayerMask mask, RayHit& outHit)
	{
		const Vec3 forward = direction.Normalized();
		if (forward.LengthSquared() < Epsilon)
			return false;

		bool found = false;
		float closest = maxDistance;
		for (auto [entity, transform, collider] : world.View<TransformComponent, ColliderComponent>().each())
		{
			if (!(mask & LayerBit(ParsePhysicsLayer(collider.Layer))))
				continue;

			// Into the collider's unscaled local space. t stays a world distance because the
			// direction is transformed the same way as the origin.
			const Quat rotation = transform.GetRotation();
			const Quat inverse = Inverse(rotation);
			const Vec3 localOrigin = SafeDivide(inverse.Rotate(origin - transform.Position), transform.Scale);
			const Vec3 localDirection = SafeDivide(inverse.Rotate(forward), transform.Scale);

			float t = 0;
			Vec3 localNormal;
			bool hit = false;
			if (collider.Shape == "Sphere")
				hit = RaySphere(localOrigin, localDirection, {}, collider.Radius, t, localNormal);
			else if (collider.Shape == "Capsule")
				hit = RayCapsule(localOrigin, localDirection, collider.Radius, collider.HalfHeight, t, localNormal);
			else
				hit = RayBox(localOrigin, localDirection, collider.HalfExtents, t, localNormal);
			if (!hit || t > closest)
				continue;

			closest = t;
			found = true;
			outHit.Entity = entity;
			outHit.Distance = t;
			outHit.Point = origin + forward * t;
			// Normals scale by the inverse of the shape's scale.
			outHit.Normal = rotation.Rotate(SafeDivide(localNormal, transform.Scale)).Normalized();
		}
		return found;
	}

	Array<Entity> OverlapSphere(World& world, Vec3 center, float radius, PhysicsLayerMask mask)
	{
		Array<Entity> touching;
		for (auto [entity, transform, collider] : world.View<TransformComponent, ColliderComponent>().each())
		{
			if (!(mask & LayerBit(ParsePhysicsLayer(collider.Layer))))
				continue;

			const Quat rotation = transform.GetRotation();
			const Vec3 local = Inverse(rotation).Rotate(center - transform.Position);
			Vec3 closest;
			float reach = radius;
			if (collider.Shape == "Sphere")
				reach += collider.Radius * MaxComponent(transform.Scale);
			else if (collider.Shape == "Capsule")
			{
				const float halfHeight = collider.HalfHeight * std::abs(transform.Scale.Y);
				closest = { 0, std::clamp(local.Y, -halfHeight, halfHeight), 0 };
				reach += collider.Radius * std::max(std::abs(transform.Scale.X), std::abs(transform.Scale.Z));
			}
			else
			{
				// Exact for boxes: clamp to the scaled extents in local space.
				const Vec3 half = collider.HalfExtents * Vec3{ std::abs(transform.Scale.X), std::abs(transform.Scale.Y), std::abs(transform.Scale.Z) };
				closest = { std::clamp(local.X, -half.X, half.X), std::clamp(local.Y, -half.Y, half.Y), std::clamp(local.Z, -half.Z, half.Z) };
			}
			if ((local - closest).LengthSquared() <= reach * reach)
				touching.Add(entity);
		}
		return touching;
	}
}
