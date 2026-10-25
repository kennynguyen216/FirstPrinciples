#pragma once

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Render/MeshData.h"

#include <memory>

namespace FP
{
	// Bones a skeletal mesh can draw with. Matches MAX_BONES in Common.glsl.
	constexpr int32 MaxSkinBones = 128;

	// Up to four bones per vertex. Matches the second vertex buffer of SkinnedMesh.vert.
	struct SkinVertex
	{
		uint16 Bones[4] = {};
		float Weights[4] = { 1, 0, 0, 0 }; // add up to 1
	};

	// Everything one glTF file holds for skeletal animation.
	struct SkeletalModel
	{
		MeshData Mesh; // in the bind pose, one section per glTF primitive
		Array<SkinVertex> Skin; // one per Mesh vertex, empty when the file has no skin
		Skeleton Bones;
		Array<AnimationClip> Clips;

		bool IsSkinned() const { return !Skin.IsEmpty() && !Bones.Bones.IsEmpty(); }

		// Null when no clip has the name.
		const AnimationClip* FindClip(std::string_view name) const;
	};

	// Reads a .gltf or .glb file, with any .bin files next to it. The first skin and the mesh
	// using it are loaded, or the first mesh when there is no skin. False when it cannot be read.
	bool LoadGltf(const char* fullPath, SkeletalModel& out);

	// Same from memory. Buffers must be embedded, as in a .glb or base64 data URIs.
	bool ParseGltf(std::string_view bytes, SkeletalModel& out);

	// Loads each glTF file once, by path relative to Content.
	class SkeletalModelLibrary
	{
	public:
		static SkeletalModelLibrary& Get();

		// Null for an empty path or a file that fails to load (logged once).
		const SkeletalModel* Load(const String& path);

		void Clear();

	private:
		HashMap<String, std::shared_ptr<SkeletalModel>> m_Models;
		HashMap<String, uint64> m_Revisions;
		Array<String> m_Failed;
	};

	// True for paths ending in .gltf or .glb.
	bool IsGltfPath(std::string_view path);
}
