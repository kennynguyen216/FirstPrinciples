#include "Tests/TestFramework.h"
#include "Engine/Render/GPUDevice.h"
#include "Engine/Render/Renderer.h"
#include "Engine/Engine.h"
#include "Engine/SceneSerializer.h"
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

FP_TEST(PostProcess_GPUSettingsApplyAcrossScenes)
{
	if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return;
	GPUDevice* device = GPUDevice::Create();
	if (!device) { SDL_QuitSubSystem(SDL_INIT_VIDEO); return; }
	Engine::RegisterEngineComponents();
	const EngineSettings original = EngineSettings::Get();
	EngineSettings::Get() = EngineSettings();
	{
		Renderer renderer(*device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB);
		FP_EXPECT(renderer.IsValid());
		StrongObjectPtr<World> world(NewObject<World>());
		Entity sprite = world->CreateEntity("Edge");
		world->Get<TransformComponent>(sprite).Rotation.Z = 31;
		auto& quad = world->Add<SpriteComponent>(sprite);
		quad.Size = { 2, 2 };
		quad.Tint = { 1, 0.15f, 0.05f, 1 };
		const Entity entity = world->CreateEntity("Post Processing");
		auto& post = world->Add<PostProcessComponent>(entity);
		auto render = [&](World& scene, uint32 width = 65, uint32 height = 49) {
			auto target = device->CreateRenderTarget(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB, width, height, false);
			Array<uint8> pixels;
			pixels.Resize(static_cast<int32>(width * height * 4));
			FP_EXPECT(target);
			if (!target) return pixels;
			auto* cmd = SDL_AcquireGPUCommandBuffer(device->GetHandle());
			renderer.Render(cmd, target->GetHandle(), width, height, scene, RenderView::Orthographic2D({}, 16, width, height));
			SDL_SubmitGPUCommandBuffer(cmd);
			FP_EXPECT(device->ReadPixels(target->GetHandle(), width, height, pixels.Data()));
			return pixels;
		};
		auto same = [](const Array<uint8>& a, const Array<uint8>& b) {
			if (a.Num() != b.Num()) return false;
			for (int32 i = 0; i < a.Num(); ++i) if (a[i] != b[i]) return false;
			return true;
		};
		const auto baseline = render(*world.Get());
		for (const char* mode : EngineSettings::ColorBlindModes())
		{
			if (String(mode) == "None") continue;
			EngineSettings::Get().ColorBlindMode = mode;
			EngineSettings::Get().ColorBlindStrength = 1;
			FP_EXPECT(!same(baseline, render(*world.Get())));
			EngineSettings::Get().ColorBlindStrength = 0;
			FP_EXPECT(same(baseline, render(*world.Get())));
		}
		EngineSettings::Get() = EngineSettings();
		post.FXAA = true;
		FP_EXPECT(!same(baseline, render(*world.Get())));
		post.FXAA = false;
		post.Bloom = true;
		post.BloomIntensity = 1;
		post.BloomThreshold = 0.25f;
		FP_EXPECT(!same(baseline, render(*world.Get())));
		post.BloomThreshold = 65504;
		FP_EXPECT(same(baseline, render(*world.Get())));
		post.BloomThreshold = 0.25f;
		post.FXAA = true;
		EngineSettings::Get().ColorBlindMode = "Deuteranopia";
		const auto combined = render(*world.Get());
		render(*world.Get(), 1, 1);
		FP_EXPECT(same(combined, render(*world.Get())));
		const String snapshot = SceneSerializer::SaveToString(*world.Get());
		StrongObjectPtr<World> other(NewObject<World>());
		FP_EXPECT(SceneSerializer::LoadFromString(*other.Get(), snapshot.View()));
		FP_EXPECT(EngineSettings::Get().ColorBlindMode == "Deuteranopia");
		FP_EXPECT(same(combined, render(*other.Get())));
	}
	EngineSettings::Get() = original;
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
