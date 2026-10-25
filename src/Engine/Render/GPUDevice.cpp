#include "Engine/Render/GPUDevice.h"
#include "Engine/Render/SDLVulkanBridge.h"
#include "Engine/Paths.h"
#include "Core/Misc/File.h"

#include <cstring>

namespace FP
{
	GPUDevice* GPUDevice::Create()
	{
		// SPIR-V covers Vulkan on Windows and Linux. MSL covers Metal on macOS.
		const SDL_GPUShaderFormat formats = SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL;
		const bool validation = FP_BUILD_DEBUG != 0 || (SDL_getenv("FP_GPU_VALIDATION") && std::strcmp(SDL_getenv("FP_GPU_VALIDATION"), "1") == 0);
		SDL_GPUDevice* handle = nullptr;
#if FP_PLATFORM_WINDOWS && FP_VULKAN_RAY_QUERY
		SDL_PropertiesID createProperties = SDL_CreateProperties();
		SDL_GPUVulkanOptions options = {};
		options.vulkan_api_version = (1u << 22) | (2u << 12);
		if (createProperties)
		{
			SDL_SetBooleanProperty(createProperties, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
			SDL_SetBooleanProperty(createProperties, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, validation);
			SDL_SetBooleanProperty(createProperties, FP_SDL_RAY_QUERY_PROPERTY, true);
			SDL_SetPointerProperty(createProperties, SDL_PROP_GPU_DEVICE_CREATE_VULKAN_OPTIONS_POINTER, &options);
			handle = SDL_CreateGPUDeviceWithProperties(createProperties);
			SDL_DestroyProperties(createProperties);
		}
#endif
		if (!handle) handle = SDL_CreateGPUDevice(formats, validation, nullptr);
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

		// Prefer 16 bit depth to reduce shadow bandwidth.
		const SDL_GPUTextureUsageFlags shadowUsage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
		if (!SDL_GPUTextureSupportsFormat(handle, SDL_GPU_TEXTUREFORMAT_D16_UNORM, SDL_GPU_TEXTURETYPE_2D, shadowUsage))
			device->m_ShadowFormat = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;

		const uint8 white[4] = { 255, 255, 255, 255 };
		const uint8 black[4] = { 0, 0, 0, 255 };
		const uint8 flatNormal[4] = { 128, 128, 255, 255 };
		device->m_White = device->CreateTexture(white, 1, 1, false, false);
		device->m_Black = device->CreateTexture(black, 1, 1, false, false);
		device->m_FlatNormal = device->CreateTexture(flatNormal, 1, 1, false, false);
		if (!device->m_LinearRepeat || !device->m_LinearClamp || !device->m_ShadowCompare || !device->m_White || !device->m_Black || !device->m_FlatNormal)
		{
			FP_LOG_ERROR("GPU initialization allocation failed: %s", SDL_GetError());
			delete device;
			return nullptr;
		}

		FP_LOG("GPU device ready (%s)", device->GetDriverName());
		const SDL_PropertiesID properties = SDL_GetGPUDeviceProperties(handle);
		FP_LOG("GPU adapter: %s, driver: %s", SDL_GetStringProperty(properties, SDL_PROP_GPU_DEVICE_NAME_STRING, "unknown"),
			SDL_GetStringProperty(properties, SDL_PROP_GPU_DEVICE_DRIVER_NAME_STRING, "unknown"));
		return device;
	}

	GPUDevice::~GPUDevice()
	{
		m_White.Reset();
		m_Black.Reset();
		m_FlatNormal.Reset();
		if (m_Download)
			SDL_ReleaseGPUTransferBuffer(m_Device, m_Download);
		if (m_LinearRepeat) SDL_ReleaseGPUSampler(m_Device, m_LinearRepeat);
		if (m_LinearClamp) SDL_ReleaseGPUSampler(m_Device, m_LinearClamp);
		if (m_ShadowCompare) SDL_ReleaseGPUSampler(m_Device, m_ShadowCompare);
		SDL_DestroyGPUDevice(m_Device);
	}

	const char* GPUDevice::GetDriverName() const
	{
		return SDL_GetGPUDeviceDriver(m_Device);
	}

	SDL_GPUShader* GPUDevice::LoadShader(const char* name, uint32 numSamplers, uint32 numUniformBuffers, uint32 numStorageBuffers)
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
		info.num_storage_buffers = numStorageBuffers;

		SDL_GPUShader* shader = SDL_CreateGPUShader(m_Device, &info);
		if (!shader)
			FP_LOG_ERROR("Shader %s failed: %s", name, SDL_GetError());
		return shader;
	}

	SDL_GPUComputePipeline* GPUDevice::LoadComputeShader(const char* name, uint32 samplers, uint32 readBuffers, uint32 writeBuffers, uint32 threadsX, uint32 threadsY)
	{
		const bool metal = m_ShaderFormat == SDL_GPU_SHADERFORMAT_MSL;
		String code;
		const String path = Paths::EngineContent(String::Format("Shaders/%s.%s", name, metal ? "msl" : "spv"));
		if (!File::ReadAllText(path.CStr(), code)) return nullptr;
		SDL_GPUComputePipelineCreateInfo info{};
		info.code = reinterpret_cast<const uint8*>(code.CStr());
		info.code_size = static_cast<usize>(code.Length());
		info.entrypoint = metal ? "main0" : "main";
		info.format = m_ShaderFormat;
		info.num_samplers = samplers;
		info.num_readonly_storage_buffers = readBuffers;
		info.num_readwrite_storage_buffers = writeBuffers;
		info.num_uniform_buffers = 1;
		info.threadcount_x = threadsX; info.threadcount_y = threadsY; info.threadcount_z = 1;
		auto* pipeline = SDL_CreateGPUComputePipeline(m_Device, &info);
		if (!pipeline) FP_LOG_WARN("Compute shader %s failed: %s", name, SDL_GetError());
		return pipeline;
	}

	RefPtr<GPUTexture> GPUDevice::CreateTexture(const void* rgba, uint32 width, uint32 height, bool srgb, bool mipmaps)
	{
		if (!rgba || !width || !height || static_cast<uint64>(width) * height > 64ull * 1024 * 1024) return nullptr;
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
		if (!transfer) { SDL_ReleaseGPUTexture(m_Device, texture); return nullptr; }
		void* mapped = SDL_MapGPUTransferBuffer(m_Device, transfer, false);
		if (!mapped) { SDL_ReleaseGPUTransferBuffer(m_Device, transfer); SDL_ReleaseGPUTexture(m_Device, texture); return nullptr; }
		std::memcpy(mapped, rgba, bytes);
		SDL_UnmapGPUTransferBuffer(m_Device, transfer);

		SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(m_Device);
		if (!cmd) { SDL_ReleaseGPUTransferBuffer(m_Device, transfer); SDL_ReleaseGPUTexture(m_Device, texture); return nullptr; }
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
		const bool submitted = SDL_SubmitGPUCommandBuffer(cmd);
		SDL_ReleaseGPUTransferBuffer(m_Device, transfer);
		if (!submitted) { SDL_ReleaseGPUTexture(m_Device, texture); return nullptr; }

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
			if (!cmd) return nullptr;
			if (!UploadToBuffer(cmd, *result, data, size)) { SDL_CancelGPUCommandBuffer(cmd); return nullptr; }
			if (!SDL_SubmitGPUCommandBuffer(cmd)) return nullptr;
		}
		return result;
	}

	bool GPUDevice::UploadToBuffer(SDL_GPUCommandBuffer* cmd, GPUBuffer& buffer, const void* data, uint32 size, bool cycle)
	{
		if (!cmd || !data || size == 0 || size > buffer.GetSize()) return false;
		SDL_GPUTransferBufferCreateInfo transferInfo = {};
		transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
		transferInfo.size = size;
		SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(m_Device, &transferInfo);
		if (!transfer) return false;
		void* mapped = SDL_MapGPUTransferBuffer(m_Device, transfer, false);
		if (!mapped) { SDL_ReleaseGPUTransferBuffer(m_Device, transfer); return false; }
		std::memcpy(mapped, data, size);
		SDL_UnmapGPUTransferBuffer(m_Device, transfer);

		SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
		SDL_GPUTransferBufferLocation source = {};
		source.transfer_buffer = transfer;
		SDL_GPUBufferRegion destination = {};
		destination.buffer = buffer.GetHandle();
		destination.size = size;
		SDL_UploadToGPUBuffer(copy, &source, &destination, cycle);
		SDL_EndGPUCopyPass(copy);

		// Safe to release now. SDL keeps it alive until the command buffer finishes.
		SDL_ReleaseGPUTransferBuffer(m_Device, transfer);
		return true;
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
		// Rows are packed tightly here. Padded rows passed straight through came out sheared on Metal.
		const uint32 packedRow = width * 4;
		SDL_GPUTransferBufferCreateInfo transferInfo = {};
		transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
		transferInfo.size = packedRow * height;
		SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(m_Device, &transferInfo);
		uint8* destination8 = static_cast<uint8*>(SDL_MapGPUTransferBuffer(m_Device, transfer, false));
		if (rowBytes == packedRow)
			std::memcpy(destination8, pixels, static_cast<size_t>(packedRow) * height);
		else
		{
			for (uint32 row = 0; row < height; ++row)
				std::memcpy(destination8 + static_cast<size_t>(row) * packedRow, static_cast<const uint8*>(pixels) + static_cast<size_t>(row) * rowBytes, packedRow);
		}
		SDL_UnmapGPUTransferBuffer(m_Device, transfer);

		SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
		SDL_GPUTextureTransferInfo source = {};
		source.transfer_buffer = transfer;
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

	bool GPUDevice::ReadPixels(SDL_GPUTexture* texture, uint32 width, uint32 height, void* outPixels, uint32 bytesPerPixel)
	{
		if (!texture || !outPixels || width == 0 || height == 0 || (bytesPerPixel != 4 && bytesPerPixel != 16) ||
			static_cast<uint64>(width) * height * bytesPerPixel > 256 * 1024 * 1024) return false;
		const uint32 bytes = width * height * bytesPerPixel;
		if (bytes > m_DownloadSize)
		{
			if (m_Download)
				SDL_ReleaseGPUTransferBuffer(m_Device, m_Download);
			SDL_GPUTransferBufferCreateInfo info = {};
			info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
			info.size = bytes;
			m_Download = SDL_CreateGPUTransferBuffer(m_Device, &info);
			if (!m_Download) { m_DownloadSize = 0; return false; }
			m_DownloadSize = bytes;
		}

		SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(m_Device);
		if (!cmd) return false;
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
		const bool completed = SDL_WaitForGPUFences(m_Device, true, &fence, 1);
		SDL_ReleaseGPUFence(m_Device, fence);
		if (!completed) return false;

		const void* mapped = SDL_MapGPUTransferBuffer(m_Device, m_Download, false);
		if (!mapped) return false;
		std::memcpy(outPixels, mapped, bytes);
		SDL_UnmapGPUTransferBuffer(m_Device, m_Download);
		return true;
	}
}
