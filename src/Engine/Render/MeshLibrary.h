#pragma once

#include "Engine/Render/GPUDevice.h"
#include "Engine/Render/Meshlets.h"
#include "Engine/Render/RayScene.h"

namespace FP
{
	// A mesh uploaded to the GPU, shared by every entity that uses the same name or path.
	class GPUMesh : public RefCounted
	{
	public:
		RefPtr<GPUBuffer> Vertices;
		RefPtr<GPUBuffer> Indices;
		uint32 IndexCount = 0;
		Array<Meshlet> Meshlets;
		Array<MeshSection> Sections; // drawn one at a time, each with its own material
		RefPtr<GPUBuffer> Skin; // SkinVertex per vertex, only on skeletal meshes
		Vec3 BoundsMin;
		Vec3 BoundsMax;
		RefPtr<RayMesh> RayGeometry;
		Array<RefPtr<RayMesh>> RaySections;
		uint64 Fingerprint = 0;
	};

	// Turns MeshComponent names into GPU meshes. Built in primitives are made in code and
	// .obj files load from Content. Each is uploaded once.
	class MeshLibrary
	{
	public:
		explicit MeshLibrary(GPUDevice& device) : m_Device(device) {}

		// Null when the name is empty or the file fails to load. Reload bypasses cached data.
		GPUMesh* Get(const String& nameOrPath, bool reload = false);

		// A glTF with its skin weights. Null when it fails to load or has no skin.
		GPUMesh* GetSkinned(const String& path);
		void ReleaseRayGeometry();
		void ReleaseUnusedRayGeometry(const Array<String>& names);

	private:
		RefPtr<GPUMesh> Upload(const MeshData& data);

		GPUDevice& m_Device;
		void CheckRevision(const String& path);
		HashMap<String, uint64> m_Revisions;
		HashMap<String, RefPtr<GPUMesh>> m_Meshes;
		HashMap<String, RefPtr<GPUMesh>> m_Skinned;
		Array<String> m_Failed;
	};
}
