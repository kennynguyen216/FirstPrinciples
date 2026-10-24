#pragma once

#include "Core/CoreMinimal.h"
#include "Engine/Math.h"

namespace FP
{
	// Vertex layout shared by sprites and meshes. Matches Mesh.vert.
	struct MeshVertex
	{
		Vec3 Position;
		Vec3 Normal;
		Vec2 UV;
		Vec4 Tangent; // w is the bitangent sign
	};

	// A run of indices drawn with one material. OBJ files start one at each usemtl.
	struct MeshSection
	{
		String Name;
		uint32 FirstIndex = 0;
		uint32 IndexCount = 0;
	};

	// Triangle list on the CPU. Front faces wind counter clockwise.
	struct MeshData
	{
		Array<MeshVertex> Vertices;
		Array<uint32> Indices;

		// Cover Indices in order with no gaps. Every loaded mesh has at least one.
		Array<MeshSection> Sections;

		Vec3 BoundsMin;
		Vec3 BoundsMax;

		void ComputeBounds();

		// Fills tangents from UVs so normal maps work. Call after positions, normals and UVs are set.
		void ComputeTangents();
	};

	namespace Primitives
	{
		// All centered on the origin with size 1.
		MeshData Cube();
		MeshData Sphere(int32 segments = 32, int32 rings = 16);
		MeshData Plane(); // XZ plane facing +Y, for floors
		MeshData Quad();  // XY plane facing +Z, like a sprite

		// Names accepted by MeshComponent.
		const Array<const char*>& GetNames();

		// True and fills out when name is a built in primitive.
		bool Create(std::string_view name, MeshData& out);
	}

	// Wavefront OBJ with positions, UVs and normals. Polygons are triangulated as fans.
	// Missing normals are smoothed from faces. Faces under the same usemtl name share a section,
	// in the order the names first show up. Returns false on a parse failure.
	bool ParseObj(std::string_view text, MeshData& out);

	// A built in primitive, or an .obj, .gltf or .glb relative to Content. glTF meshes come in
	// their bind pose. False when it cannot be loaded.
	bool LoadMesh(std::string_view nameOrPath, MeshData& out);
}
