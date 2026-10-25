#pragma once
#include "Engine/Render/MeshData.h"

namespace FP
{
	constexpr uint32 MeshletMaxVertices = 64;
	constexpr uint32 MeshletMaxTriangles = 124;
	struct Meshlet
	{
		Vec3 BoundsMin;
		Vec3 BoundsMax;
		uint32 FirstIndex = 0;
		uint32 IndexCount = 0;
		uint32 Section = 0;
		uint32 VertexCount = 0;
	};
	struct MeshletData
	{
		Array<Meshlet> Meshlets;
		Array<uint32> Indices;
		Array<MeshSection> Sections;
	};
	// Optimizes each material section independently. Vertex order is retained for skin weights.
	// Rejects malformed topology before passing it to meshoptimizer.
	bool BuildMeshlets(const MeshData& mesh, MeshletData& out);
}
