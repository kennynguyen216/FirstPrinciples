#include "Engine/Render/RayScene.h"

#include <algorithm>
#include <limits>

namespace FP
{
	static_assert(sizeof(RayNode) == 48);
	static_assert(sizeof(RayTriangle) == 192);

	namespace
	{
		float Axis(Vec3 p, int axis) { return axis == 0 ? p.X : axis == 1 ? p.Y : p.Z; }
		Vec3 Minimum(Vec3 a, Vec3 b) { return { std::min(a.X, b.X), std::min(a.Y, b.Y), std::min(a.Z, b.Z) }; }
		Vec3 Maximum(Vec3 a, Vec3 b) { return { std::max(a.X, b.X), std::max(a.Y, b.Y), std::max(a.Z, b.Z) }; }
		RayBounds TriangleBounds(const RayTriangle& t)
		{
			return { Minimum(t.Position[0].XYZ(), Minimum(t.Position[1].XYZ(), t.Position[2].XYZ())),
				Maximum(t.Position[0].XYZ(), Maximum(t.Position[1].XYZ(), t.Position[2].XYZ())) };
		}

		template <typename T, typename BoundsFn>
		uint32 BuildNodes(Array<RayNode>& nodes, Array<T>& items, uint32 first, uint32 count, BoundsFn boundsOf)
		{
			const uint32 index = static_cast<uint32>(nodes.Num());
			nodes.Add({});
			RayBounds bounds = boundsOf(items[static_cast<int32>(first)]);
			for (uint32 i = first + 1; i < first + count; ++i)
			{
				const RayBounds b = boundsOf(items[static_cast<int32>(i)]);
				bounds.Min = Minimum(bounds.Min, b.Min);
				bounds.Max = Maximum(bounds.Max, b.Max);
			}
			nodes[static_cast<int32>(index)].Min = { bounds.Min, 0 };
			nodes[static_cast<int32>(index)].Max = { bounds.Max, 0 };
			if (count <= 4)
			{
				nodes[static_cast<int32>(index)].First = first;
				nodes[static_cast<int32>(index)].Count = count;
			}
			else
			{
				const Vec3 extent = bounds.Max - bounds.Min;
				const int axis = extent.X >= extent.Y && extent.X >= extent.Z ? 0 : extent.Y >= extent.Z ? 1 : 2;
				const uint32 middle = first + count / 2;
				std::nth_element(items.begin() + first, items.begin() + middle, items.begin() + first + count,
					[&](const T& a, const T& b) { const auto ba = boundsOf(a); const auto bb = boundsOf(b);
						return Axis(ba.Min + ba.Max, axis) < Axis(bb.Min + bb.Max, axis); });
				BuildNodes(nodes, items, first, middle - first, boundsOf);
				nodes[static_cast<int32>(index)].Right = BuildNodes(nodes, items, middle, first + count - middle, boundsOf);
			}
			nodes[static_cast<int32>(index)].Escape = static_cast<uint32>(nodes.Num());
			return index;
		}

		bool IntersectTriangle(const Ray& ray, const RayTriangle& t, float maximum, float& distance, Vec3& bary)
		{
			const Vec3 absolute{ std::fabs(ray.Direction.X), std::fabs(ray.Direction.Y), std::fabs(ray.Direction.Z) };
			const int zAxis = absolute.X > absolute.Y ? (absolute.X > absolute.Z ? 0 : 2) : (absolute.Y > absolute.Z ? 1 : 2);
			int xAxis = (zAxis + 1) % 3, yAxis = (xAxis + 1) % 3;
			if (Axis(ray.Direction, zAxis) < 0) std::swap(xAxis, yAxis);
			const float sx = -Axis(ray.Direction, xAxis) / Axis(ray.Direction, zAxis);
			const float sy = -Axis(ray.Direction, yAxis) / Axis(ray.Direction, zAxis);
			const float sz = 1 / Axis(ray.Direction, zAxis);
			float x[3], y[3], z[3];
			for (int i = 0; i < 3; ++i)
			{
				const Vec3 p = t.Position[i].XYZ() - ray.Origin;
				x[i] = Axis(p, xAxis) + sx * Axis(p, zAxis);
				y[i] = Axis(p, yAxis) + sy * Axis(p, zAxis);
				z[i] = Axis(p, zAxis) * sz;
			}
			const float a = x[1] * y[2] - y[1] * x[2];
			const float b = x[2] * y[0] - y[2] * x[0];
			const float c = x[0] * y[1] - y[0] * x[1];
			if ((a < 0 || b < 0 || c < 0) && (a > 0 || b > 0 || c > 0)) return false;
			const float determinant = a + b + c;
			if (determinant == 0) return false;
			const float d = (a * z[0] + b * z[1] + c * z[2]) / determinant;
			if (!std::isfinite(d) || d < 1e-4f || d >= maximum)
				return false;
			distance = d;
			bary = { a / determinant, b / determinant, c / determinant };
			return true;
		}
	}

	bool IsFinite(Vec3 p) { return std::isfinite(p.X) && std::isfinite(p.Y) && std::isfinite(p.Z); }

	bool IsFinite(const Mat4& value)
	{
		const float* p = reinterpret_cast<const float*>(&value);
		for (int i = 0; i < 16; ++i)
		{
			if (!std::isfinite(p[i]))
				return false;
		}
		return true;
	}

	bool RayBounds::Intersect(const Ray& ray, float minimum, float maximum, float* exit) const
	{
		for (int axis = 0; axis < 3; ++axis)
		{
			const float d = Axis(ray.Direction, axis);
			const float o = Axis(ray.Origin, axis);
			if (std::fabs(d) < 1e-20f)
			{
				if (o < Axis(Min, axis) || o > Axis(Max, axis))
					return false;
				continue;
			}
			float a = (Axis(Min, axis) - o) / d;
			float b = (Axis(Max, axis) - o) / d;
			if (a > b)
				std::swap(a, b);
			minimum = std::max(minimum, a);
			maximum = std::min(maximum, b);
			if (maximum < minimum)
				return false;
		}
		if (exit)
			*exit = maximum;
		return true;
	}

	RefPtr<RayMesh> RayMesh::Build(const MeshData& data, uint32 firstIndex, uint32 indexCount)
	{
		if (firstIndex > static_cast<uint32>(data.Indices.Num()) || firstIndex % 3 != 0)
			return nullptr;
		if (indexCount == UINT32_MAX) indexCount = static_cast<uint32>(data.Indices.Num()) - firstIndex;
		if (indexCount > static_cast<uint32>(data.Indices.Num()) - firstIndex || indexCount % 3 != 0 || indexCount / 3 > RayScene::MaxTriangles)
			return nullptr;
		RefPtr<RayMesh> mesh = MakeRef<RayMesh>();
		mesh->SourceTriangleCount = indexCount / 3;
		for (uint32 first = firstIndex; first < firstIndex + indexCount; first += 3)
		{
			RayTriangle triangle;
			triangle.Primitive = (first - firstIndex) / 3;
			Vec2 uv[3];
			for (int i = 0; i < 3; ++i)
			{
				const uint32 index = data.Indices[first + i];
				if (index >= static_cast<uint32>(data.Vertices.Num()))
					return nullptr;
				const MeshVertex& v = data.Vertices[static_cast<int32>(index)];
				if (!IsFinite(v.Position) || !IsFinite(v.Normal) || !IsFinite(v.Tangent.XYZ()) ||
					!std::isfinite(v.Tangent.W) || !std::isfinite(v.UV.X) || !std::isfinite(v.UV.Y))
					return nullptr;
				triangle.Position[i] = { v.Position, 0 };
				triangle.Normal[i] = { v.Normal, 0 };
				triangle.Tangent[i] = v.Tangent;
				uv[i] = v.UV;
			}
			triangle.UV01 = { uv[0].X, uv[0].Y, uv[1].X, uv[1].Y };
			triangle.UV2 = { uv[2].X, uv[2].Y, 0, 0 };
			const Vec3 cross = Vec3::Cross(triangle.Position[1].XYZ() - triangle.Position[0].XYZ(), triangle.Position[2].XYZ() - triangle.Position[0].XYZ());
			if (!IsFinite(cross) || !std::isfinite(cross.LengthSquared())) return nullptr;
			if (cross.LengthSquared() > 0)
				mesh->Triangles.Add(triangle);
		}
		if (!mesh->Triangles.IsEmpty())
		{
			BuildNodes(mesh->Nodes, mesh->Triangles, 0, static_cast<uint32>(mesh->Triangles.Num()), TriangleBounds);
			mesh->Bounds = { mesh->Nodes[0].Min.XYZ(), mesh->Nodes[0].Max.XYZ() };
		}
		return mesh;
	}

	bool RayScene::Publish(Array<RayInstance> instances, bool complete)
	{
		if (instances.Num() > static_cast<int32>(MaxInstances))
			return false;
		for (int32 i = 0; i < instances.Num(); ++i)
		{
			RayInstance& instance = instances[i];
			if (!instance.Mesh || !IsFinite(instance.Transform))
				return false;
			const Vec3 x = instance.Transform.TransformVector({ 1, 0, 0 });
			const Vec3 y = instance.Transform.TransformVector({ 0, 1, 0 });
			const Vec3 z = instance.Transform.TransformVector({ 0, 0, 1 });
			const float determinant = Vec3::Dot(x, Vec3::Cross(y, z));
			if (!std::isfinite(determinant) || std::fabs(determinant) < 1e-12f)
				return false;
			instance.Inverse = instance.Transform.Inverse();
			if (!IsFinite(instance.Inverse))
				return false;
			for (int32 j = 0; j < i; ++j)
			{
				if (instance.Identity == instances[j].Identity)
					return false;
			}
			const RayBounds local = instance.Mesh->Bounds;
			instance.Bounds.Min = { std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
			instance.Bounds.Max = -instance.Bounds.Min;
			for (int corner = 0; corner < 8; ++corner)
			{
				const Vec3 p = instance.Transform.TransformPoint({ (corner & 1) ? local.Max.X : local.Min.X,
					(corner & 2) ? local.Max.Y : local.Min.Y, (corner & 4) ? local.Max.Z : local.Min.Z });
				if (!IsFinite(p)) return false;
				instance.Bounds.Min = Minimum(instance.Bounds.Min, p);
				instance.Bounds.Max = Maximum(instance.Bounds.Max, p);
			}
		}
		Array<RayNode> nodes;
		if (!instances.IsEmpty())
			BuildNodes(nodes, instances, 0, static_cast<uint32>(instances.Num()), [](const RayInstance& i) { return i.Bounds; });
		m_Instances = std::move(instances);
		m_Nodes = std::move(nodes);
		m_Complete = complete;
		++m_Revision;
		return true;
	}

	RayHit RayScene::Trace(const Ray& ray, float maximum, bool segment, uint32 budget) const
	{
		RayHit hit;
		if (!IsFinite(ray.Origin) || !IsFinite(ray.Direction) || !std::isfinite(maximum) || maximum <= 0 ||
			std::fabs(ray.Direction.LengthSquared() - 1) > 1e-3f)
			return hit;
		hit.Distance = maximum;
		hit.Class = m_Complete ? (segment ? RayResultClass::ClearSegment : RayResultClass::Escaped) : RayResultClass::UnknownCoverage;
		if (!segment && !m_Nodes.IsEmpty())
		{
			float exit = 0;
			const RayBounds bounds{ m_Nodes[0].Min.XYZ(), m_Nodes[0].Max.XYZ() };
			if (bounds.Intersect(ray, 0, std::numeric_limits<float>::max(), &exit) && exit > maximum)
				hit.Class = RayResultClass::UnknownCoverage;
		}
		for (uint32 nodeIndex = 0; nodeIndex < static_cast<uint32>(m_Nodes.Num());)
		{
			if (++hit.Visits > budget)
			{
				hit.Class = RayResultClass::Exhausted;
				return hit;
			}
			const RayNode& node = m_Nodes[static_cast<int32>(nodeIndex)];
			if (!RayBounds{ node.Min.XYZ(), node.Max.XYZ() }.Intersect(ray, 1e-4f, hit.Distance))
			{
				nodeIndex = node.Escape;
				continue;
			}
			++nodeIndex;
			for (uint32 i = node.First; i < node.First + node.Count; ++i)
			{
				const RayInstance& instance = m_Instances[static_cast<int32>(i)];
				const Ray local{ instance.Inverse.TransformPoint(ray.Origin), instance.Inverse.TransformVector(ray.Direction) };
				for (uint32 meshIndex = 0; meshIndex < static_cast<uint32>(instance.Mesh->Nodes.Num());)
				{
					if (++hit.Visits > budget)
					{
						hit.Class = RayResultClass::Exhausted;
						return hit;
					}
					const RayNode& meshNode = instance.Mesh->Nodes[static_cast<int32>(meshIndex)];
					if (!RayBounds{ meshNode.Min.XYZ(), meshNode.Max.XYZ() }.Intersect(local, 1e-4f, hit.Distance))
					{
						meshIndex = meshNode.Escape;
						continue;
					}
					++meshIndex;
					for (uint32 t = meshNode.First; t < meshNode.First + meshNode.Count; ++t)
					{
						const RayTriangle& triangle = instance.Mesh->Triangles[static_cast<int32>(t)];
						float distance;
						Vec3 bary;
						if (!IntersectTriangle(local, triangle, hit.Distance, distance, bary))
							continue;
						hit.Class = RayResultClass::Surface;
						hit.Identity = instance.Identity;
						hit.Instance = i;
						hit.Triangle = t;
						hit.Primitive = triangle.Primitive;
						hit.Distance = distance;
						hit.Barycentrics = bary;
						hit.Position = ray.Origin + ray.Direction * distance;
						const Vec3 edge1 = instance.Transform.TransformVector(triangle.Position[1].XYZ() - triangle.Position[0].XYZ());
						const Vec3 edge2 = instance.Transform.TransformVector(triangle.Position[2].XYZ() - triangle.Position[0].XYZ());
						hit.GeometricNormal = Vec3::Cross(edge1, edge2).Normalized();
						const Vec3 localNormal = triangle.Normal[0].XYZ() * bary.X + triangle.Normal[1].XYZ() * bary.Y + triangle.Normal[2].XYZ() * bary.Z;
						hit.ShadingNormal = instance.Inverse.Transposed().TransformVector(localNormal).Normalized();
						if (Vec3::Dot(hit.ShadingNormal, hit.GeometricNormal) < 0)
							hit.ShadingNormal = -hit.ShadingNormal;
						hit.UV = Vec2(triangle.UV01.X, triangle.UV01.Y) * bary.X + Vec2(triangle.UV01.Z, triangle.UV01.W) * bary.Y + Vec2(triangle.UV2.X, triangle.UV2.Y) * bary.Z;
						if (segment) return hit;
					}
				}
			}
		}
		return hit;
	}

	bool RayScene::IsVisible(Vec3 from, Vec3 to, Vec3 geometricNormal) const
	{
		const Vec3 delta = to - from;
		const float distance = delta.Length();
		if (!IsFinite(from) || !IsFinite(to) || !IsFinite(geometricNormal) || distance <= 2e-4f)
			return false;
		const Vec3 direction = delta / distance;
		const Vec3 origin = from + geometricNormal * (Vec3::Dot(direction, geometricNormal) >= 0 ? 1e-4f : -1e-4f);
		return Trace({ origin, direction }, distance - 2e-4f, true).Class == RayResultClass::ClearSegment;
	}

	usize RayScene::GetMemoryBytes() const
	{
		usize bytes = static_cast<usize>(m_Nodes.Num()) * sizeof(RayNode) + static_cast<usize>(m_Instances.Num()) * sizeof(RayInstance);
		Array<const RayMesh*> meshes;
		for (const RayInstance& instance : m_Instances)
		{
			if (meshes.Contains(instance.Mesh.Get()))
				continue;
			meshes.Add(instance.Mesh.Get());
			bytes += static_cast<usize>(instance.Mesh->Nodes.Num()) * sizeof(RayNode) + static_cast<usize>(instance.Mesh->Triangles.Num()) * sizeof(RayTriangle);
		}
		return bytes;
	}

	float PunctualAttenuation(float distance, float range)
	{
		if (!std::isfinite(distance) || !std::isfinite(range) || distance < 0 || range <= 0)
			return 0;
		const float ratio = distance / range;
		const float window = std::max(0.0f, 1 - ratio * ratio * ratio * ratio);
		return window * window / std::max(distance * distance, 0.0001f);
	}

	float SpotAttenuation(float cosine, float innerDegrees, float outerDegrees)
	{
		if (!std::isfinite(cosine) || !std::isfinite(innerDegrees) || !std::isfinite(outerDegrees) ||
			innerDegrees < 0 || outerDegrees <= innerDegrees || outerDegrees >= 90)
			return 0;
		const float outer = std::cos(outerDegrees * DegToRad);
		const float inner = std::cos(innerDegrees * DegToRad);
		const float t = std::clamp((cosine - outer) / (inner - outer), 0.0f, 1.0f);
		return t * t * (3 - 2 * t);
	}

	uint64 MeshFingerprint(const MeshData& mesh)
	{
		uint64 hash = 1469598103934665603ull;
		auto add = [&](const void* data, usize size) {
			const uint8* bytes = static_cast<const uint8*>(data);
			for (usize i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 1099511628211ull;
		};
		add(mesh.Vertices.Data(), static_cast<usize>(mesh.Vertices.Num()) * sizeof(MeshVertex));
		add(mesh.Indices.Data(), static_cast<usize>(mesh.Indices.Num()) * sizeof(uint32));
		return hash;
	}
}
