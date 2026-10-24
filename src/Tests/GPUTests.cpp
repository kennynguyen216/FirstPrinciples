#include "Tests/TestFramework.h"
#include "Engine/Render/GPUDevice.h"

#include <SDL3/SDL.h>

using namespace FP;

FP_TEST(GPU_UploadsPaddedRowsIntoOddSizes)
{
	// Skipped where there is no GPU, like some build machines.
	if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
		return;
	GPUDevice* device = GPUDevice::Create();
	if (!device)
	{
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
		return;
	}

	// 5 by 3 pixels in rows padded to 32 bytes, the way UI pages hand them over.
	constexpr uint32 width = 5;
	constexpr uint32 height = 3;
	constexpr uint32 rowBytes = 32;
	uint8 source[rowBytes * height] = {};
	for (uint32 y = 0; y < height; ++y)
	{
		for (uint32 x = 0; x < width; ++x)
		{
			uint8* pixel = source + y * rowBytes + x * 4;
			pixel[0] = static_cast<uint8>(x * 40);
			pixel[1] = static_cast<uint8>(y * 80);
			pixel[2] = 7;
			pixel[3] = 255;
		}
	}

	RefPtr<GPUTexture> texture = device->CreateDynamicTexture(SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM, width, height);
	FP_EXPECT(texture);
	if (texture)
	{
		SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device->GetHandle());
		device->UploadToTexture(cmd, *texture, source, rowBytes);
		SDL_SubmitGPUCommandBuffer(cmd);

		// Every pixel lands where it belongs, with the padding dropped.
		uint8 read[width * height * 4] = {};
		FP_EXPECT(device->ReadPixels(texture->GetHandle(), width, height, read));
		bool matches = true;
		for (uint32 y = 0; y < height; ++y)
		{
			for (uint32 x = 0; x < width; ++x)
			{
				const uint8* pixel = read + (y * width + x) * 4;
				matches &= pixel[0] == x * 40 && pixel[1] == y * 80 && pixel[2] == 7 && pixel[3] == 255;
			}
		}
		FP_EXPECT(matches);
	}
	texture.Reset();
	delete device;
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
