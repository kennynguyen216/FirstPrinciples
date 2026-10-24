#include "Engine/Render/MeshLibrary.h"
#include "Engine/Paths.h"

namespace FP
{
	GPUMesh* MeshLibrary::Get(const String& nameOrPath)
	{
		if (nameOrPath.IsEmpty())
			return nullptr;
		if (RefPtr<GPUMesh>* cached = m_Meshes.Find(nameOrPath))
			return cached->Get();
		if (m_Failed.Contains(nameOrPath))
			return nullptr;

		MeshData data;
		if (!LoadMesh(nameOrPath.View(), data))
		{
			FP_LOG_WARN("Could not load mesh %s", Paths::Content(nameOrPath.View()).CStr());
			m_Failed.Add(nameOrPath);
			return nullptr;
		}

		RefPtr<GPUMesh> mesh = MakeRef<GPUMesh>();
		mesh->Vertices = m_Device.CreateBuffer(SDL_GPU_BUFFERUSAGE_VERTEX, data.Vertices.Data(), static_cast<uint32>(data.Vertices.Num()) * sizeof(MeshVertex));
		mesh->Indices = m_Device.CreateBuffer(SDL_GPU_BUFFERUSAGE_INDEX, data.Indices.Data(), static_cast<uint32>(data.Indices.Num()) * sizeof(uint32));
		mesh->IndexCount = static_cast<uint32>(data.Indices.Num());
		mesh->Sections = data.Sections;
		mesh->BoundsMin = data.BoundsMin;
		mesh->BoundsMax = data.BoundsMax;
		m_Meshes.Add(nameOrPath, mesh);
		return mesh.Get();
	}
}
