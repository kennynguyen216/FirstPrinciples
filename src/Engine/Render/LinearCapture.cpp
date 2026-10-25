#include "Engine/Render/LinearCapture.h"
#include "Core/Misc/File.h"

#include <bit>

namespace FP
{
	bool SaveLinearRGBA(const char* path, const Array<Vec4>& pixels, uint32 width, uint32 height)
	{
		if (!width || !height || static_cast<uint64>(width) * height != static_cast<uint64>(pixels.Num())) return false;
		for (const auto& pixel : pixels) if (!std::isfinite(pixel.X) || !std::isfinite(pixel.Y) || !std::isfinite(pixel.Z) || !std::isfinite(pixel.W)) return false;
		String bytes = String::Format("FPRGBA32F\n%u %u\n", width, height);
		bytes.Append(std::string_view(reinterpret_cast<const char*>(pixels.Data()), static_cast<usize>(pixels.Num()) * sizeof(Vec4)));
		return File::WriteAllText(path, bytes.View());
	}

	Array<Vec4> CaptureLinear(GPUDevice& device, GPUTexture& source)
	{
		Array<Vec4> pixels;
		const uint32 width = source.GetWidth(), height = source.GetHeight();
		if (static_cast<uint64>(width) * height > 16 * 1024 * 1024) return pixels;
		auto target = device.CreateRenderTarget(SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT, width, height, false);
		if (!target) return pixels;
		SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device.GetHandle());
		if (!cmd) return pixels;
		SDL_GPUBlitInfo blit = {};
		blit.source.texture = source.GetHandle();
		blit.source.w = width;
		blit.source.h = height;
		blit.destination.texture = target->GetHandle();
		blit.destination.w = width;
		blit.destination.h = height;
		blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
		blit.filter = SDL_GPU_FILTER_NEAREST;
		SDL_BlitGPUTexture(cmd, &blit);
		if (!SDL_SubmitGPUCommandBuffer(cmd)) return pixels;
		pixels.Resize(static_cast<int32>(width * height));
		if (!device.ReadPixels(target->GetHandle(), width, height, pixels.Data(), 16)) pixels.Clear();
		return pixels;
	}

	bool SaveLinearPFM(const char* path, const Array<Vec4>& pixels, uint32 width, uint32 height)
	{
		if (width == 0 || height == 0 || static_cast<uint64>(width) * height != static_cast<uint64>(pixels.Num())) return false;
		String bytes = String::Format("PF\n%u %u\n%s1.0\n", width, height, std::endian::native == std::endian::little ? "-" : "");
		for (uint32 y = height; y-- > 0;)
		{
			for (uint32 x = 0; x < width; ++x)
			{
				const Vec4& p = pixels[static_cast<int32>(y * width + x)];
				if (!std::isfinite(p.X) || !std::isfinite(p.Y) || !std::isfinite(p.Z)) { FP_LOG_ERROR("Nonfinite HDR pixel %u,%u in %s", x, y, path); return false; }
				const float rgb[3] = { p.X, p.Y, p.Z };
				bytes.Append(std::string_view(reinterpret_cast<const char*>(rgb), sizeof(rgb)));
			}
		}
		return File::WriteAllText(path, bytes.View());
	}
}
