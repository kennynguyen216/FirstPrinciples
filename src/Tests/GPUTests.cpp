#include "Tests/TestFramework.h"
#include "Engine/Render/GPUDevice.h"
#include "Engine/Render/MeshLibrary.h"
#include "Engine/Paths.h"
#include "Core/Misc/File.h"
#include "Core/Misc/Guid.h"

#include <SDL3/SDL.h>

#include <filesystem>

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

FP_TEST(GPU_MeshReloadPicksUpEditsAndRetriesFailures)
{
	if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
		return;
	GPUDevice* device = GPUDevice::Create();
	if (!device)
	{
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
		return;
	}

	namespace fs = std::filesystem;
	const fs::path file = fs::temp_directory_path() / (std::string("FPMeshReload_") + Guid::New().ToString().CStr() + ".obj");
	const String relative(fs::relative(file, Paths::GetContentDir().CStr()).generic_string().c_str());
	const String fullPath(file.generic_string().c_str());
	{
		MeshLibrary meshes(*device);
		FP_EXPECT(!meshes.Get(relative));
		FP_EXPECT(File::WriteAllText(fullPath.CStr(), "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n"));
		FP_EXPECT(!meshes.Get(relative));
		const GPUMesh* first = meshes.Get(relative, true);
		FP_EXPECT(first && first->BoundsMax.X == 1);

		FP_EXPECT(File::WriteAllText(fullPath.CStr(), "v 0 0 0\nv 2 0 0\nv 0 1 0\nf 1 2 3\n"));
		FP_EXPECT(meshes.Get(relative) == first);
		const GPUMesh* changed = meshes.Get(relative, true);
		FP_EXPECT(changed && changed->BoundsMax.X == 2);
		FP_EXPECT(meshes.Get(relative) == changed);
	}
	std::error_code ec;
	fs::remove(file, ec);
	delete device;
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
