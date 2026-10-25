#include "Engine/Render/MeshletCulling.h"

namespace FP
{
	static_assert(sizeof(MeshletCullItem) == 112);
	static_assert(sizeof(SDL_GPUIndexedIndirectDrawCommand) == 20);
	MeshletCulling::MeshletCulling(GPUDevice& device) : m_Device(device)
	{
		m_Reduce = device.LoadComputeShader("MeshletDepth.comp", 1, 0, 1, 8, 8);
		m_Cull = device.LoadComputeShader("MeshletCull.comp", 0, 2, 2, 64, 1);
	}
	MeshletCulling::~MeshletCulling()
	{
		auto* gpu = m_Device.GetHandle();
		if (m_Fence) { SDL_WaitForGPUFences(gpu, true, &m_Fence, 1); SDL_ReleaseGPUFence(gpu, m_Fence); }
		if (m_Download) SDL_ReleaseGPUTransferBuffer(gpu, m_Download);
		if (m_Cull) SDL_ReleaseGPUComputePipeline(gpu, m_Cull);
		if (m_Reduce) SDL_ReleaseGPUComputePipeline(gpu, m_Reduce);
	}
	bool MeshletCulling::Dispatch(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* depth, uint32 width, uint32 height,
		const Array<MeshletCullItem>& items, const MeshletSettings& settings)
	{
		m_TriangleCounts.Clear();
		if (!IsValid() || items.IsEmpty()) return false;
		const uint32 count = static_cast<uint32>(items.Num());
		if (count > UINT32_MAX / sizeof(MeshletCullItem)) return false;
		const uint32 tileWidth = (width + 7) / 8, tileHeight = (height + 7) / 8;
		auto ensure = [&](RefPtr<GPUBuffer>& buffer, uint32 bytes, SDL_GPUBufferUsageFlags usage) {
			if (!buffer || buffer->GetSize() < bytes) buffer = m_Device.CreateBuffer(usage, nullptr, bytes);
			return static_cast<bool>(buffer);
		};
		if (!ensure(m_Items, count * sizeof(MeshletCullItem), SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ) ||
			!ensure(m_Commands, count * 20, SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_INDIRECT) ||
			!ensure(m_Reasons, count * 4, SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE) ||
			!ensure(m_Tiles, tileWidth * tileHeight * 4, SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ)) return false;
		m_Device.UploadToBuffer(cmd, *m_Items, items.Data(), count * sizeof(MeshletCullItem));
		struct Uniforms { uint32 Width, Height, Count, Flags; } uniforms{ width, height, count, (settings.Frustum ? 1u : 0u) | (settings.Occlusion ? 2u : 0u) };
		SDL_PushGPUComputeUniformData(cmd, 0, &uniforms, sizeof(uniforms));
		// One max-depth value for each 8x8 tile. Edge padding is far depth, never an occluder.
		SDL_GPUStorageBufferReadWriteBinding tileBinding{ m_Tiles->GetHandle(), true };
		auto* pass = SDL_BeginGPUComputePass(cmd, nullptr, 0, &tileBinding, 1);
		SDL_BindGPUComputePipeline(pass, m_Reduce);
		SDL_GPUTextureSamplerBinding sampled{ depth, m_Device.GetClampSampler() };
		SDL_BindGPUComputeSamplers(pass, 0, &sampled, 1);
		SDL_DispatchGPUCompute(pass, tileWidth, tileHeight, 1);
		SDL_EndGPUComputePass(pass);
		SDL_GPUStorageBufferReadWriteBinding outputs[2] = { { m_Commands->GetHandle(), true }, { m_Reasons->GetHandle(), true } };
		pass = SDL_BeginGPUComputePass(cmd, nullptr, 0, outputs, 2);
		SDL_BindGPUComputePipeline(pass, m_Cull);
		SDL_GPUBuffer* inputs[2] = { m_Items->GetHandle(), m_Tiles->GetHandle() };
		SDL_BindGPUComputeStorageBuffers(pass, 0, inputs, 2);
		SDL_DispatchGPUCompute(pass, (count + 63) / 64, 1, 1);
		SDL_EndGPUComputePass(pass);
		for (const auto& item : items) m_TriangleCounts.Add(item.IndexCount / 3);
		return true;
	}
	const MeshletStats& MeshletCulling::ReadStats()
	{
		auto* gpu = m_Device.GetHandle();
		if (m_Fence && SDL_QueryGPUFence(gpu, m_Fence))
		{
			const auto* reasons = static_cast<const uint32*>(SDL_MapGPUTransferBuffer(gpu, m_Download, false));
			if (reasons)
			{
				m_Stats = {};
				m_Stats.Total = static_cast<uint32>(m_PendingTriangles.Num());
				for (int32 i = 0; i < m_PendingTriangles.Num(); ++i)
				{
					m_Stats.Triangles += m_PendingTriangles[i];
					if (reasons[i] == 0) { ++m_Stats.Visible; m_Stats.VisibleTriangles += m_PendingTriangles[i]; }
					else if (reasons[i] == 1) ++m_Stats.Frustum;
					else ++m_Stats.Occluded;
				}
				SDL_UnmapGPUTransferBuffer(gpu, m_Download);
			}
			SDL_ReleaseGPUFence(gpu, m_Fence); m_Fence = nullptr;
			SDL_ReleaseGPUTransferBuffer(gpu, m_Download); m_Download = nullptr;
		}
		if (!m_Fence && !m_TriangleCounts.IsEmpty())
		{
			SDL_GPUTransferBufferCreateInfo info{};
			info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
			info.size = static_cast<uint32>(m_TriangleCounts.Num()) * 4;
			m_Download = SDL_CreateGPUTransferBuffer(gpu, &info);
			if (!m_Download) return m_Stats;
			auto* cmd = SDL_AcquireGPUCommandBuffer(gpu);
			if (!cmd) { SDL_ReleaseGPUTransferBuffer(gpu, m_Download); m_Download = nullptr; return m_Stats; }
			auto* copy = SDL_BeginGPUCopyPass(cmd);
			SDL_GPUBufferRegion source{ m_Reasons->GetHandle(), 0, info.size };
			SDL_GPUTransferBufferLocation dest{ m_Download, 0 };
			SDL_DownloadFromGPUBuffer(copy, &source, &dest);
			SDL_EndGPUCopyPass(copy);
			m_PendingTriangles = m_TriangleCounts;
			m_Fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
		}
		return m_Stats;
	}
}
