#include "Engine/Render/Meshlets.h"
#include <meshoptimizer.h>
#include <vector>

namespace FP
{
	bool BuildMeshlets(const MeshData& mesh, MeshletData& out)
	{
		out = {};
		if (mesh.Vertices.IsEmpty() || mesh.Indices.IsEmpty() || mesh.Indices.Num() % 3 != 0)
			return false;
		for (const auto& v : mesh.Vertices)
			if (!std::isfinite(v.Position.X) || !std::isfinite(v.Position.Y) || !std::isfinite(v.Position.Z)) return false;
		for (uint32 i : mesh.Indices)
			if (i >= static_cast<uint32>(mesh.Vertices.Num())) return false;
		Array<MeshSection> sections = mesh.Sections;
		if (sections.IsEmpty()) sections.Add({ "Default", 0, static_cast<uint32>(mesh.Indices.Num()) });
		uint64 end = 0;
		for (const auto& section : sections)
		{
			if (section.FirstIndex != end || section.IndexCount % 3 != 0) return false;
			end += section.IndexCount;
			if (end > static_cast<uint64>(mesh.Indices.Num())) return false;
		}
		if (end != static_cast<uint64>(mesh.Indices.Num())) return false;
		const float* positions = &mesh.Vertices[0].Position.X;
		for (int32 sectionIndex = 0; sectionIndex < sections.Num(); ++sectionIndex)
		{
			const auto& section = sections[sectionIndex];
			out.Sections.Add({ section.Name, static_cast<uint32>(out.Indices.Num()), section.IndexCount });
			if (!section.IndexCount) continue;
			std::vector<unsigned int> optimized(section.IndexCount);
			meshopt_optimizeVertexCache(optimized.data(), mesh.Indices.Data() + section.FirstIndex, section.IndexCount, mesh.Vertices.Num());
			const size_t bound = meshopt_buildMeshletsBound(section.IndexCount, MeshletMaxVertices, MeshletMaxTriangles);
			std::vector<meshopt_Meshlet> clusters(bound);
			std::vector<unsigned int> vertices(bound * MeshletMaxVertices);
			std::vector<unsigned char> triangles(bound * MeshletMaxTriangles * 3);
			const size_t count = meshopt_buildMeshlets(clusters.data(), vertices.data(), triangles.data(), optimized.data(), optimized.size(),
				positions, mesh.Vertices.Num(), sizeof(MeshVertex), MeshletMaxVertices, MeshletMaxTriangles, 0.0f);
			for (size_t i = 0; i < count; ++i)
			{
				const auto& c = clusters[i];
				meshopt_optimizeMeshlet(vertices.data() + c.vertex_offset, triangles.data() + c.triangle_offset, c.triangle_count, c.vertex_count);
				Meshlet m;
				m.FirstIndex = static_cast<uint32>(out.Indices.Num());
				m.IndexCount = c.triangle_count * 3;
				m.Section = static_cast<uint32>(sectionIndex);
				m.VertexCount = c.vertex_count;
				m.BoundsMin = m.BoundsMax = mesh.Vertices[vertices[c.vertex_offset]].Position;
				for (uint32 j = 0; j < c.vertex_count; ++j)
				{
					const Vec3 p = mesh.Vertices[vertices[c.vertex_offset + j]].Position;
					m.BoundsMin = { std::fmin(m.BoundsMin.X, p.X), std::fmin(m.BoundsMin.Y, p.Y), std::fmin(m.BoundsMin.Z, p.Z) };
					m.BoundsMax = { std::fmax(m.BoundsMax.X, p.X), std::fmax(m.BoundsMax.Y, p.Y), std::fmax(m.BoundsMax.Z, p.Z) };
				}
				for (uint32 j = 0; j < m.IndexCount; ++j)
					out.Indices.Add(vertices[c.vertex_offset + triangles[c.triangle_offset + j]]);
				out.Meshlets.Add(m);
			}
		}
		return true;
	}
}
