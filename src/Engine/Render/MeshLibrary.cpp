#include "Engine/Render/MeshLibrary.h"
#include "Engine/Animation/SkeletalModel.h"
#include "Engine/Paths.h"
#include "Engine/Assets/AssetDatabase.h"

namespace FP
{
	void MeshLibrary::ReleaseUnusedRayGeometry(const Array<String>& names)
	{
		Array<String> references;
		for (const String& name : names) references.Add(Paths::AssetReference(name));
		for (auto& pair : m_Meshes) if (!references.Contains(pair.Key))
		{
			pair.Value->RayGeometry.Reset();
			pair.Value->RaySections.Clear();
		}
	}

	void MeshLibrary::ReleaseRayGeometry()
	{
		for (auto& pair : m_Meshes)
		{
			pair.Value->RayGeometry.Reset();
			pair.Value->RaySections.Clear();
		}
	}

	void MeshLibrary::CheckRevision(const String& path)
	{
		const uint64 revision = Paths::GetAssets().Fingerprint(path);
		if (const uint64* previous = m_Revisions.Find(path); previous && *previous == revision) return;
		m_Meshes.Remove(path);
		m_Skinned.Remove(path);
		m_Failed.Remove(path);
		m_Revisions.Add(path, revision);
	}

	GPUMesh* MeshLibrary::Get(const String& reference, bool reload)
	{
		const String nameOrPath = Paths::AssetReference(reference);
		if (nameOrPath.IsEmpty())
			return nullptr;
		CheckRevision(nameOrPath);
		if (!reload)
		{
			if (RefPtr<GPUMesh>* cached = m_Meshes.Find(nameOrPath))
				return cached->Get();
			if (m_Failed.Contains(nameOrPath))
				return nullptr;
		}

		MeshData data;
		bool loaded = false;
		if (reload && IsGltfPath(nameOrPath.View()))
		{
			// Thumbnail refreshes read the file without changing the shared animation cache.
			SkeletalModel model;
			loaded = LoadGltf(Paths::Content(nameOrPath).CStr(), model);
			data = std::move(model.Mesh);
		}
		else
			loaded = LoadMesh(nameOrPath.View(), data);
		if (!loaded)
		{
			FP_LOG_WARN("Could not load mesh %s", Paths::Content(nameOrPath.View()).CStr());
			m_Failed.Add(nameOrPath);
			return nullptr;
		}

		RefPtr<GPUMesh> mesh = Upload(data);
		if (!mesh) return nullptr;
		m_Failed.Remove(nameOrPath);
		m_Meshes.Add(nameOrPath, mesh);
		return mesh.Get();
	}

	GPUMesh* MeshLibrary::GetSkinned(const String& reference)
	{
		const String path = Paths::AssetReference(reference);
		CheckRevision(path);
		if (RefPtr<GPUMesh>* cached = m_Skinned.Find(path))
			return cached->Get();
		const SkeletalModel* model = SkeletalModelLibrary::Get().Load(path);
		if (!model || !model->IsSkinned())
			return nullptr;

		RefPtr<GPUMesh> mesh = Upload(model->Mesh);
		if (!mesh) return nullptr;
		mesh->Skin = m_Device.CreateBuffer(SDL_GPU_BUFFERUSAGE_VERTEX, model->Skin.Data(), static_cast<uint32>(model->Skin.Num()) * sizeof(SkinVertex));
		if (!mesh->Skin) return nullptr;
		m_Skinned.Add(path, mesh);
		return mesh.Get();
	}

	RefPtr<GPUMesh> MeshLibrary::Upload(const MeshData& data)
	{
		if (static_cast<uint64>(data.Vertices.Num()) * sizeof(MeshVertex) > UINT32_MAX ||
			static_cast<uint64>(data.Indices.Num()) * sizeof(uint32) > UINT32_MAX) return nullptr;
		MeshletData clusters;
		if (!BuildMeshlets(data, clusters))
		{
			FP_LOG_WARN("Invalid mesh topology; upload rejected.");
			return nullptr;
		}
		RefPtr<GPUMesh> mesh = MakeRef<GPUMesh>();
		mesh->Fingerprint = MeshFingerprint(data);
		mesh->Vertices = m_Device.CreateBuffer(SDL_GPU_BUFFERUSAGE_VERTEX, data.Vertices.Data(), static_cast<uint32>(data.Vertices.Num()) * sizeof(MeshVertex));
		mesh->Indices = m_Device.CreateBuffer(SDL_GPU_BUFFERUSAGE_INDEX, clusters.Indices.Data(), static_cast<uint32>(data.Indices.Num()) * sizeof(uint32));
		mesh->IndexCount = static_cast<uint32>(data.Indices.Num());
		mesh->Sections = std::move(clusters.Sections);
		mesh->Meshlets = std::move(clusters.Meshlets);
		if (!mesh->Vertices || !mesh->Indices) return nullptr;
		mesh->BoundsMin = data.BoundsMin;
		mesh->BoundsMax = data.BoundsMax;
		return mesh;
	}
}
