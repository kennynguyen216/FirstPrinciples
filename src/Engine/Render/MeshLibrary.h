#pragma once

#include "Engine/Render/GPUDevice.h"
#include "Engine/Render/Meshlets.h"

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
	};

	// Turns MeshComponent names into GPU meshes. Built in primitives are made in code and
	// .obj files load from Content. Each is uploaded once.
	class MeshLibrary
	{
	public:
		explicit MeshLibrary(GPUDevice& device) : m_Device(device) {}

		// Null when the name is empty or the file fails to load (logged once).
		GPUMesh* Get(const String& nameOrPath);

		// A glTF with its skin weights. Null when it fails to load or has no skin.
		GPUMesh* GetSkinned(const String& path);

	private:
		RefPtr<GPUMesh> Upload(const MeshData& data);

		GPUDevice& m_Device;
		HashMap<String, RefPtr<GPUMesh>> m_Meshes;
		HashMap<String, RefPtr<GPUMesh>> m_Skinned;
		Array<String> m_Failed;
	};
}
