#pragma once

#include "Core/CoreMinimal.h"

#include <SDL3/SDL_gpu.h>

namespace FP
{
	class GPUDevice;

	// A GPU texture kept alive by RefPtr so materials and sprites can share it.
	class GPUTexture : public RefCounted
	{
	public:
		GPUTexture(SDL_GPUDevice* device, SDL_GPUTexture* texture, uint32 width, uint32 height)
			: m_Device(device), m_Texture(texture), m_Width(width), m_Height(height) {}
		~GPUTexture() override { SDL_ReleaseGPUTexture(m_Device, m_Texture); }

		SDL_GPUTexture* GetHandle() const { return m_Texture; }
		uint32 GetWidth() const { return m_Width; }
		uint32 GetHeight() const { return m_Height; }

	private:
		SDL_GPUDevice* m_Device;
		SDL_GPUTexture* m_Texture;
		uint32 m_Width;
		uint32 m_Height;
	};

	// A GPU buffer with a fixed size, released with its RefPtr.
	class GPUBuffer : public RefCounted
	{
	public:
		GPUBuffer(SDL_GPUDevice* device, SDL_GPUBuffer* buffer, uint32 size) : m_Device(device), m_Buffer(buffer), m_Size(size) {}
		~GPUBuffer() override { SDL_ReleaseGPUBuffer(m_Device, m_Buffer); }

		SDL_GPUBuffer* GetHandle() const { return m_Buffer; }
		uint32 GetSize() const { return m_Size; }

	private:
		SDL_GPUDevice* m_Device;
		SDL_GPUBuffer* m_Buffer;
		uint32 m_Size;
	};

	// Owns the SDL_GPU device and the things every renderer needs from it.
	// Shaders ship precompiled in Engine/Content/Shaders as SPIR-V (Vulkan) and MSL (Metal).
	class GPUDevice
	{
	public:
		// Returns null and logs when no supported GPU backend is available.
		static GPUDevice* Create();
		~GPUDevice();

		GPUDevice(const GPUDevice&) = delete;
		GPUDevice& operator=(const GPUDevice&) = delete;

		SDL_GPUDevice* GetHandle() const { return m_Device; }
		const char* GetDriverName() const;

		// Loads Engine/Content/Shaders/<name>.spv or .msl depending on the backend.
		SDL_GPUShader* LoadShader(const char* name, uint32 numSamplers, uint32 numUniformBuffers);

		// Compute shaders use one uniform buffer; resource counts follow SDL binding order.
		SDL_GPUComputePipeline* LoadComputeShader(const char* name, uint32 samplers, uint32 readBuffers, uint32 writeBuffers, uint32 threadsX, uint32 threadsY);

		// Uploads tightly packed RGBA8 pixels. Mipmaps are generated on the GPU when asked.
		RefPtr<GPUTexture> CreateTexture(const void* rgba, uint32 width, uint32 height, bool srgb, bool mipmaps);

		// sampledDepth lets shaders read a depth target, as shadow maps need.
		RefPtr<GPUTexture> CreateRenderTarget(SDL_GPUTextureFormat format, uint32 width, uint32 height, bool depth, bool sampledDepth = false);

		RefPtr<GPUBuffer> CreateBuffer(SDL_GPUBufferUsageFlags usage, const void* data, uint32 size);

		// Copies data into a buffer with a copy pass on cmd. The buffer must be large enough.
		void UploadToBuffer(SDL_GPUCommandBuffer* cmd, GPUBuffer& buffer, const void* data, uint32 size);

		// A texture refilled from the CPU every so often, like a UI page. 4 bytes per pixel.
		RefPtr<GPUTexture> CreateDynamicTexture(SDL_GPUTextureFormat format, uint32 width, uint32 height);

		// Copies rows of pixels into a dynamic texture with a copy pass on cmd. rowBytes may be
		// wider than the texture when the source pads its rows, and the padding is dropped.
		void UploadToTexture(SDL_GPUCommandBuffer* cmd, GPUTexture& texture, const void* pixels, uint32 rowBytes);

		// Copies a 4 byte per pixel texture to the CPU. Waits for the GPU so use it for the editor and screenshots.
		bool ReadPixels(SDL_GPUTexture* texture, uint32 width, uint32 height, void* outPixels);

		SDL_GPUSampler* GetDefaultSampler() const { return m_LinearRepeat; }
		SDL_GPUSampler* GetClampSampler() const { return m_LinearClamp; }
		SDL_GPUTextureFormat GetDepthFormat() const { return m_DepthFormat; }

		// Clamped, filtered depth comparison for sampler2DShadow.
		SDL_GPUSampler* GetShadowSampler() const { return m_ShadowCompare; }
		SDL_GPUTextureFormat GetShadowFormat() const { return m_ShadowFormat; }

		// 1x1 fallbacks so shaders can always sample every slot.
		GPUTexture* GetWhiteTexture() const { return m_White.Get(); }
		GPUTexture* GetBlackTexture() const { return m_Black.Get(); }
		GPUTexture* GetFlatNormalTexture() const { return m_FlatNormal.Get(); }

	private:
		GPUDevice() = default;

		SDL_GPUDevice* m_Device = nullptr;
		SDL_GPUShaderFormat m_ShaderFormat = SDL_GPU_SHADERFORMAT_INVALID;
		SDL_GPUSampler* m_LinearRepeat = nullptr;
		SDL_GPUSampler* m_LinearClamp = nullptr;
		SDL_GPUTextureFormat m_DepthFormat = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
		SDL_GPUSampler* m_ShadowCompare = nullptr;
		SDL_GPUTextureFormat m_ShadowFormat = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
		RefPtr<GPUTexture> m_White;
		RefPtr<GPUTexture> m_Black;
		RefPtr<GPUTexture> m_FlatNormal;

		SDL_GPUTransferBuffer* m_Download = nullptr;
		uint32 m_DownloadSize = 0;
	};
}
