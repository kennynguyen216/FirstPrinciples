#include "Engine/Render/GPUDevice.h"
#include "Engine/Paths.h"
#include "Core/Misc/File.h"

#include <cstring>

namespace FP
{
	GPUDevice* GPUDevice::Create()
	{
		// SPIR-V covers Vulkan on Windows and Linux. MSL covers Metal on macOS.
		const SDL_GPUShaderFormat formats = SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL;
		SDL_GPUDevice* handle = SDL_CreateGPUDevice(formats, FP_BUILD_DEBUG != 0, nullptr);
		if (!handle)
		{
			FP_LOG_ERROR("No supported GPU backend (needs Vulkan or Metal): %s", SDL_GetError());
			return nullptr;
		}

		GPUDevice* device = new GPUDevice();
		device->m_Device = handle;
		const SDL_GPUShaderFormat available = SDL_GetGPUShaderFormats(handle);
		device->m_ShaderFormat = (available & SDL_GPU_SHADERFORMAT_MSL) ? SDL_GPU_SHADERFORMAT_MSL : SDL_GPU_SHADERFORMAT_SPIRV;

		SDL_GPUSamplerCreateInfo sampler = {};
		sampler.min_filter = SDL_GPU_FILTER_LINEAR;
		sampler.mag_filter = SDL_GPU_FILTER_LINEAR;
		sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
		sampler.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
		sampler.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
		sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
		sampler.max_lod = 1000;
		sampler.enable_anisotropy = true;
		sampler.max_anisotropy = 8;
		device->m_LinearRepeat = SDL_CreateGPUSampler(handle, &sampler);

		// Screen sized textures like the scene copy must not wrap at the edges.
		sampler.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		sampler.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		sampler.enable_anisotropy = false;
		device->m_LinearClamp = SDL_CreateGPUSampler(handle, &sampler);

		// Compares against depth and filters the 2x2 results, so one tap gives soft shadow edges.
		sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
		sampler.max_lod = 0;
		sampler.enable_compare = true;
		sampler.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
		device->m_ShadowCompare = SDL_CreateGPUSampler(handle, &sampler);

		// D32 float works on every backend SDL_GPU supports but check anyway.
		if (!SDL_GPUTextureSupportsFormat(handle, SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET))
			device->m_DepthFormat = SDL_GPU_TEXTUREFORMAT_D24_UNORM;

		// Shadow maps only need 16 bits since their depth is linear, and it halves their bandwidth.
		const SDL_GPUTextureUsageFlags shadowUsage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
		if (!SDL_GPUTextureSupportsFormat(handle, SDL_GPU_TEXTUREFORMAT_D16_UNORM, SDL_GPU_TEXTURETYPE_2D, shadowUsage))
			device->m_ShadowFormat = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;

		const uint8 white[4] = { 255, 255, 255, 255 };
		const uint8 black[4] = { 0, 0, 0, 255 };
		const uint8 flatNormal[4] = { 128, 128, 255, 255 };
		device->m_White = device->CreateTexture(white, 1, 1, false, false);
		device->m_Black = device->CreateTexture(black, 1, 1, false, false);
		device->m_FlatNormal = device->CreateTexture(flatNormal, 1, 1, false, false);

		FP_LOG("GPU device ready (%s)", device->GetDriverName());
		return device;
	}

	GPUDevice::~GPUDevice()
	{
		m_White.Reset();
		m_Black.Reset();
		m_FlatNormal.Reset();
		if (m_Download)
			SDL_ReleaseGPUTransferBuffer(m_Device, m_Download);
		SDL_ReleaseGPUSampler(m_Device, m_LinearRepeat);
		SDL_ReleaseGPUSampler(m_Device, m_LinearClamp);
		SDL_ReleaseGPUSampler(m_Device, m_ShadowCompare);
		SDL_DestroyGPUDevice(m_Device);
	}

	const char* GPUDevice::GetDriverName() const
	{
		return SDL_GetGPUDeviceDriver(m_Device);
	}

	SDL_GPUShader* GPUDevice::LoadShader(const char* name, uint32 numSamplers, uint32 numUniformBuffers)
	{
		const bool isMsl = m_ShaderFormat == SDL_GPU_SHADERFORMAT_MSL;
		const String path = Paths::EngineContent(String::Format("Shaders/%s.%s", name, isMsl ? "msl" : "spv"));

		String code;
		if (!File::ReadAllText(path.CStr(), code))
		{
			FP_LOG_ERROR("Missing shader %s. Run Scripts/CompileShaders.", path.CStr());
			return nullptr;
		}

		SDL_GPUShaderCreateInfo info = {};
		info.code = reinterpret_cast<const uint8*>(code.CStr());
		info.code_size = static_cast<usize>(code.Length());
		info.entrypoint = isMsl ? "main0" : "main";
		info.format = m_ShaderFormat;
		info.stage = String(name).EndsWith(".frag") ? SDL_GPU_SHADERSTAGE_FRAGMENT : SDL_GPU_SHADERSTAGE_VERTEX;
		info.num_samplers = numSamplers;
		info.num_uniform_buffers = numUniformBuffers;

		SDL_GPUShader* shader = SDL_CreateGPUShader(m_Device, &info);
		if (!shader)
			FP_LOG_ERROR("Shader %s failed: %s", name, SDL_GetError());
		return shader;
	}

	RefPtr<GPUTexture> GPUDevice::CreateTexture(const void* rgba, uint32 width, uint32 height, bool srgb, bool mipmaps)
	{
		uint32 levels = 1;
		if (mipmaps)
		{
			for (uint32 size = width > height ? width : height; size > 1; size >>= 1)
				++levels;
		}

		SDL_GPUTextureCreateInfo info = {};
		info.type = SDL_GPU_TEXTURETYPE_2D;
		info.format = srgb ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
		info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | (levels > 1 ? SDL_GPU_TEXTUREUSAGE_COLOR_TARGET : 0);
		info.width = width;
		info.height = height;
		info.layer_count_or_depth = 1;
		info.num_levels = levels;
		SDL_GPUTexture* texture = SDL_CreateGPUTexture(m_Device, &info);
		if (!texture)
		{
			FP_LOG_ERROR("Could not create texture: %s", SDL_GetError());
			return nullptr;
		}

		const uint32 bytes = width * height * 4;
		SDL_GPUTransferBufferCreateInfo transferInfo = {};
		transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
		transferInfo.size = bytes;
		SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(m_Device, &transferInfo);
		std::memcpy(SDL_MapGPUTransferBuffer(m_Device, transfer, false), rgba, bytes);
		SDL_UnmapGPUTransferBuffer(m_Device, transfer);

		SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(m_Device);
		SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
		SDL_GPUTextureTransferInfo source = {};
		source.transfer_buffer = transfer;
		SDL_GPUTextureRegion destination = {};
		destination.texture = texture;
		destination.w = width;
		destination.h = height;
		destination.d = 1;
		SDL_UploadToGPUTexture(copy, &source, &destination, false);
		SDL_EndGPUCopyPass(copy);
		if (levels > 1)
			SDL_GenerateMipmapsForGPUTexture(cmd, texture);
		SDL_SubmitGPUCommandBuffer(cmd);
		SDL_ReleaseGPUTransferBuffer(m_Device, transfer);

		return MakeRef<GPUTexture>(m_Device, texture, width, height);
	}

	RefPtr<GPUTexture> GPUDevice::CreateRenderTarget(SDL_GPUTextureFormat format, uint32 width, uint32 height, bool depth, bool sampledDepth)
	{
		SDL_GPUTextureCreateInfo info = {};
		info.type = SDL_GPU_TEXTURETYPE_2D;
		info.format = format;
		info.usage = depth ? SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET : (SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
		if (depth && sampledDepth)
			info.usage |= SDL_GPU_TEXTUREUSAGE_SAMPLER;
		info.width = width;
		info.height = height;
		info.layer_count_or_depth = 1;
		info.num_levels = 1;
		SDL_GPUTexture* texture = SDL_CreateGPUTexture(m_Device, &info);
		if (!texture)
		{
			FP_LOG_ERROR("Could not create render target: %s", SDL_GetError());
			return nullptr;
		}
		return MakeRef<GPUTexture>(m_Device, texture, width, height);
	}

	RefPtr<GPUBuffer> GPUDevice::CreateBuffer(SDL_GPUBufferUsageFlags usage, const void* data, uint32 size)
	{
		SDL_GPUBufferCreateInfo info = {};
		info.usage = usage;
		info.size = size;
		SDL_GPUBuffer* buffer = SDL_CreateGPUBuffer(m_Device, &info);
		if (!buffer)
		{
			FP_LOG_ERROR("Could not create buffer: %s", SDL_GetError());
			return nullptr;
		}
		RefPtr<GPUBuffer> result = MakeRef<GPUBuffer>(m_Device, buffer, size);
		if (data)
		{
			SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(m_Device);
			UploadToBuffer(cmd, *result, data, size);
			SDL_SubmitGPUCommandBuffer(cmd);
		}
		return result;
	}

	void GPUDevice::UploadToBuffer(SDL_GPUCommandBuffer* cmd, GPUBuffer& buffer, const void* data, uint32 size)
	{
		FP_ASSERT(size <= buffer.GetSize());
		SDL_GPUTransferBufferCreateInfo transferInfo = {};
		transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
		transferInfo.size = size;
		SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(m_Device, &transferInfo);
		std::memcpy(SDL_MapGPUTransferBuffer(m_Device, transfer, false), data, size);
		SDL_UnmapGPUTransferBuffer(m_Device, transfer);

		SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
		SDL_GPUTransferBufferLocation source = {};
		source.transfer_buffer = transfer;
		SDL_GPUBufferRegion destination = {};
		destination.buffer = buffer.GetHandle();
		destination.size = size;
		SDL_UploadToGPUBuffer(copy, &source, &destination, false);
		SDL_EndGPUCopyPass(copy);

		// Safe to release now. SDL keeps it alive until the command buffer finishes.
		SDL_ReleaseGPUTransferBuffer(m_Device, transfer);
	}

	RefPtr<GPUTexture> GPUDevice::CreateDynamicTexture(SDL_GPUTextureFormat format, uint32 width, uint32 height)
	{
		SDL_GPUTextureCreateInfo info = {};
		info.type = SDL_GPU_TEXTURETYPE_2D;
		info.format = format;
		info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
		info.width = width;
		info.height = height;
		info.layer_count_or_depth = 1;
		info.num_levels = 1;
		SDL_GPUTexture* texture = SDL_CreateGPUTexture(m_Device, &info);
		if (!texture)
		{
			FP_LOG_ERROR("Could not create texture: %s", SDL_GetError());
			return nullptr;
		}
		return MakeRef<GPUTexture>(m_Device, texture, width, height);
	}

	void GPUDevice::UploadToTexture(SDL_GPUCommandBuffer* cmd, GPUTexture& texture, const void* pixels, uint32 rowBytes)
	{
		const uint32 width = texture.GetWidth();
		const uint32 height = texture.GetHeight();
		const uint32 bytes = rowBytes * height;
		SDL_GPUTransferBufferCreateInfo transferInfo = {};
		transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
		transferInfo.size = bytes;
		SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(m_Device, &transferInfo);
		std::memcpy(SDL_MapGPUTransferBuffer(m_Device, transfer, false), pixels, bytes);
		SDL_UnmapGPUTransferBuffer(m_Device, transfer);

		SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
		SDL_GPUTextureTransferInfo source = {};
		source.transfer_buffer = transfer;
		source.pixels_per_row = rowBytes / 4;
		source.rows_per_layer = height;
		SDL_GPUTextureRegion destination = {};
		destination.texture = texture.GetHandle();
		destination.w = width;
		destination.h = height;
		destination.d = 1;
		SDL_UploadToGPUTexture(copy, &source, &destination, false);
		SDL_EndGPUCopyPass(copy);
		// SDL keeps it alive until the command buffer finishes.
		SDL_ReleaseGPUTransferBuffer(m_Device, transfer);
	}

	bool GPUDevice::ReadPixels(SDL_GPUTexture* texture, uint32 width, uint32 height, void* outPixels)
	{
		const uint32 bytes = width * height * 4;
		if (bytes > m_DownloadSize)
		{
			if (m_Download)
				SDL_ReleaseGPUTransferBuffer(m_Device, m_Download);
			SDL_GPUTransferBufferCreateInfo info = {};
			info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
			info.size = bytes;
			m_Download = SDL_CreateGPUTransferBuffer(m_Device, &info);
			m_DownloadSize = bytes;
		}

		SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(m_Device);
		SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
		SDL_GPUTextureRegion source = {};
		source.texture = texture;
		source.w = width;
		source.h = height;
		source.d = 1;
		SDL_GPUTextureTransferInfo destination = {};
		destination.transfer_buffer = m_Download;
		SDL_DownloadFromGPUTexture(copy, &source, &destination);
		SDL_EndGPUCopyPass(copy);

		SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
		if (!fence)
			return false;
		SDL_WaitForGPUFences(m_Device, true, &fence, 1);
		SDL_ReleaseGPUFence(m_Device, fence);

		const void* mapped = SDL_MapGPUTransferBuffer(m_Device, m_Download, false);
		std::memcpy(outPixels, mapped, bytes);
		SDL_UnmapGPUTransferBuffer(m_Device, m_Download);
		return true;
	}
}
