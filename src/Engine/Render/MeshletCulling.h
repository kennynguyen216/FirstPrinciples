#pragma once
#include "Engine/Render/GPUDevice.h"
#include "Engine/Math.h"

namespace FP
{
	struct MeshletSettings
	{
		bool Enabled = true;
		bool Frustum = true;
		bool Occlusion = true;
		bool Colorize = false;
		bool FreezeCamera = false;
	};
	// std430 layout shared with MeshletCull.comp.
	struct MeshletCullItem
	{
		Mat4 ClipFromLocal;
		Vec4 BoundsMin;
		Vec4 BoundsMax;
		uint32 FirstIndex = 0, IndexCount = 0, FirstInstance = 0, Padding = 0;
	};
	struct MeshletStats
	{
		uint32 Total = 0, Visible = 0, Frustum = 0, Occluded = 0;
		uint64 Triangles = 0, VisibleTriangles = 0;
	};
	class MeshletCulling
	{
	public:
		explicit MeshletCulling(GPUDevice& device);
		~MeshletCulling();
		bool IsValid() const { return m_Cull && m_Reduce; }
		bool Dispatch(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* depth, uint32 width, uint32 height,
			const Array<MeshletCullItem>& items, const MeshletSettings& settings);
		SDL_GPUBuffer* Commands() const { return m_Commands ? m_Commands->GetHandle() : nullptr; }
		// Nonblocking debug readback; returns the last completed snapshot.
		const MeshletStats& ReadStats();
	private:
		GPUDevice& m_Device;
		SDL_GPUComputePipeline* m_Cull = nullptr;
		SDL_GPUComputePipeline* m_Reduce = nullptr;
		RefPtr<GPUBuffer> m_Items, m_Commands, m_Reasons, m_Tiles;
		Array<uint32> m_TriangleCounts, m_PendingTriangles;
		SDL_GPUTransferBuffer* m_Download = nullptr;
		SDL_GPUFence* m_Fence = nullptr;
		MeshletStats m_Stats;
	};
}
