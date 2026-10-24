#include "Engine/Render/MeshLibrary.h"
#include "Engine/Paths.h"
#include "Core/Misc/File.h"

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
		if (!Primitives::Create(nameOrPath, data))
		{
			String text;
			const String fullPath = Paths::Content(nameOrPath);
			if (!File::ReadAllText(fullPath.CStr(), text) || !ParseObj(text.View(), data))
			{
				FP_LOG_WARN("Could not load mesh %s", fullPath.CStr());
				m_Failed.Add(nameOrPath);
				return nullptr;
			}
		}

		RefPtr<GPUMesh> mesh = MakeRef<GPUMesh>();
		mesh->Vertices = m_Device.CreateBuffer(SDL_GPU_BUFFERUSAGE_VERTEX, data.Vertices.Data(), static_cast<uint32>(data.Vertices.Num()) * sizeof(MeshVertex));
		mesh->Indices = m_Device.CreateBuffer(SDL_GPU_BUFFERUSAGE_INDEX, data.Indices.Data(), static_cast<uint32>(data.Indices.Num()) * sizeof(uint32));
		mesh->IndexCount = static_cast<uint32>(data.Indices.Num());
		mesh->BoundsMin = data.BoundsMin;
		mesh->BoundsMax = data.BoundsMax;
		m_Meshes.Add(nameOrPath, mesh);
		return mesh.Get();
	}
}
