#include "Tests/TestFramework.h"
#include "Engine/Render/Meshlets.h"
#include <array>
#include <set>
#include <limits>

using namespace FP;
namespace
{
	using Triangle = std::array<uint32, 3>;
	std::multiset<Triangle> Triangles(const uint32* indices, uint32 count)
	{
		std::multiset<Triangle> result;
		for (uint32 i = 0; i < count; i += 3)
		{
			Triangle t{ indices[i], indices[i + 1], indices[i + 2] };
			// A cyclic rotation preserves winding; sorting individual indices would not.
			Triangle b{ t[1], t[2], t[0] }, c{ t[2], t[0], t[1] };
			result.insert(std::min(t, std::min(b, c)));
		}
		return result;
	}
}
FP_TEST(Meshlet_PreservesSectionsTopologyAndBounds)
{
	MeshData mesh = Primitives::Sphere(64, 32);
	const uint32 count = static_cast<uint32>(mesh.Indices.Num());
	mesh.Sections = { { "A", 0, count / 2 }, { "Empty", count / 2, 0 }, { "B", count / 2, count / 2 } };
	MeshletData result;
	FP_EXPECT(BuildMeshlets(mesh, result));
	FP_EXPECT(result.Meshlets.Num() > 1 && result.Indices.Num() == mesh.Indices.Num());
	FP_EXPECT(result.Sections.Num() == 3);
	for (int32 i = 0; i < mesh.Sections.Num(); ++i)
	{
		const auto& a = mesh.Sections[i]; const auto& b = result.Sections[i];
		FP_EXPECT(a.Name == b.Name && a.IndexCount == b.IndexCount);
		FP_EXPECT(Triangles(mesh.Indices.Data() + a.FirstIndex, a.IndexCount) == Triangles(result.Indices.Data() + b.FirstIndex, b.IndexCount));
	}
	uint32 covered = 0;
	for (const auto& m : result.Meshlets)
	{
		FP_EXPECT(m.FirstIndex == covered && m.IndexCount % 3 == 0 && m.IndexCount <= MeshletMaxTriangles * 3);
		std::set<uint32> vertices;
		for (uint32 i = 0; i < m.IndexCount; ++i)
		{
			const uint32 index = result.Indices[m.FirstIndex + i];
			vertices.insert(index);
			const auto p = mesh.Vertices[index].Position;
			FP_EXPECT(p.X >= m.BoundsMin.X && p.X <= m.BoundsMax.X && p.Y >= m.BoundsMin.Y && p.Y <= m.BoundsMax.Y && p.Z >= m.BoundsMin.Z && p.Z <= m.BoundsMax.Z);
		}
		FP_EXPECT(vertices.size() == m.VertexCount && vertices.size() <= MeshletMaxVertices);
		const auto& section = result.Sections[m.Section];
		FP_EXPECT(m.FirstIndex >= section.FirstIndex && m.FirstIndex + m.IndexCount <= section.FirstIndex + section.IndexCount);
		covered += m.IndexCount;
	}
	FP_EXPECT(covered == count);
}
FP_TEST(Meshlet_ValidatesUploadsAndSynthesizesDefaultSection)
{
	MeshData mesh = Primitives::Cube();
	MeshletData result;
	FP_EXPECT(BuildMeshlets(mesh, result));
	FP_EXPECT(result.Sections.Num() == 1 && result.Sections[0].Name == "Default");
	mesh.Indices[0] = static_cast<uint32>(mesh.Vertices.Num());
	FP_EXPECT(!BuildMeshlets(mesh, result) && result.Meshlets.IsEmpty());
	mesh = Primitives::Cube();
	mesh.Sections = { { "Bad", 3, static_cast<uint32>(mesh.Indices.Num() - 3) } };
	FP_EXPECT(!BuildMeshlets(mesh, result));
	mesh = Primitives::Cube(); mesh.Indices.Add(0);
	FP_EXPECT(!BuildMeshlets(mesh, result));
	mesh = Primitives::Cube(); mesh.Vertices[0].Position.X = std::numeric_limits<float>::quiet_NaN();
	FP_EXPECT(!BuildMeshlets(mesh, result));
	FP_EXPECT(!BuildMeshlets({}, result));
}
