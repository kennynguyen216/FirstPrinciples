#include "Tests/TestFramework.h"
#include "Tests/TestProject.h"
#include "Engine/Engine.h"
#include "Engine/Material.h"
#include "Engine/Render/Renderer.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdlib>

using namespace FP;

FP_TEST(BlackHole_RenderDiskHorizonAndResize)
{
	if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return;
	auto* device = GPUDevice::Create();
	if (!device) { SDL_QuitSubSystem(SDL_INIT_VIDEO); return; }
	{
		Engine::RegisterEngineComponents();
		StrongObjectPtr<World> world(NewObject<World>());
		const Entity hole = world->CreateEntity("Lens");
		world->Get<TransformComponent>(hole).Position = { 0, 0, -20 };
		world->Get<TransformComponent>(hole).Rotation.X = 65;
		world->Add<BlackHoleComponent>(hole).StarBrightness = 0;
		Renderer renderer(*device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
		FP_EXPECT(renderer.IsValid());
		const bool benchmark = std::getenv("FP_BLACK_HOLE_BENCHMARK") != nullptr;
		for (uint32 width : { 161u, 1u, 801u, 193u, 960u, 1920u, 2560u })
		{
			if (width >= 960 && !benchmark) continue;
			const uint32 height = std::max(1u, width * 9 / 16);
			auto target = device->CreateRenderTarget(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, width, height, false);
			RenderView view;
			view.Width = width; view.Height = height; view.Orthographic = false;
			view.Projection = Mat4::Perspective(60, float(width) / height, 0.1f, 1000);
			view.Background = { 0.25f, 0.25f, 0.25f, 1 };
			view.Tonemap = false;
			auto render = [&] {
				auto* cmd = SDL_AcquireGPUCommandBuffer(device->GetHandle());
				renderer.Render(cmd, target->GetHandle(), width, height, *world.Get(), view);
				auto* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
				SDL_WaitForGPUFences(device->GetHandle(), true, &fence, 1);
				SDL_ReleaseGPUFence(device->GetHandle(), fence);
			};
			render();
			if (benchmark && width >= 960)
			{
				const auto begin = SDL_GetTicksNS();
				for (int i = 0; i < 12; ++i) render();
				FP_LOG("Black hole %ux%u: %.2f ms/frame (GPU fence, complete renderer)", width, height, (SDL_GetTicksNS() - begin) / 12000000.0);
				if (width == 2560)
				{
					world->Get<BlackHoleComponent>(hole).StarBrightness = 1;
					Array<Entity> others;
					for (int i = 0; i < 3; ++i)
					{
						const Entity other = world->CreateEntity("Other lens");
						world->Get<TransformComponent>(other).Position = { float(i * 12 - 12), 8, -35 };
						world->Get<TransformComponent>(other).Rotation.X = 65;
						world->Add<BlackHoleComponent>(other);
						others.Add(other);
					}
					render();
					const auto start = SDL_GetTicksNS();
					for (int i = 0; i < 12; ++i) render();
					FP_LOG("Four black holes with stars %ux%u: %.2f ms/frame", width, height, (SDL_GetTicksNS() - start) / 12000000.0);
					for (Entity other : others) world->DestroyEntity(other);
					world->Get<BlackHoleComponent>(hole).StarBrightness = 0;
					render();
				}
			}
			Array<uint8> pixels;
			pixels.Resize(width * height * 4);
			FP_EXPECT(device->ReadPixels(target->GetHandle(), width, height, pixels.Data()));
			if (width > 1)
			{
				// The horizon is dark, while the tilted disk must still emit visible light.
				FP_EXPECT(pixels[((height / 2) * width + width / 2) * 4] < 5);
				int brightest = 0;
				for (int i = 0; i < pixels.Num(); i += 4) brightest = std::max(brightest, int(pixels[i]));
				FP_EXPECT(brightest > 100);
			}
			if (const char* path = std::getenv("FP_BLACK_HOLE_IMAGE"); path && width == 960)
			{
				auto* surface = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, pixels.Data(), width * 4);
				SDL_SaveBMP(surface, path);
				SDL_DestroySurface(surface);
			}
		}
	}
	delete device;
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

FP_TEST(BlackHole_RenderKeepsForegroundTextureSharp)
{
	if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return;
	auto* device = GPUDevice::Create();
	if (!device) { SDL_QuitSubSystem(SDL_INIT_VIDEO); return; }
	{
		Test::ScopedProject project;
		Engine::RegisterEngineComponents();
		// Fine stripes distinguish sampling full-resolution color from upscaling a small color buffer.
		auto* surface = SDL_CreateSurface(256, 1, SDL_PIXELFORMAT_RGBA32);
		for (int x = 0; x < 256; ++x)
		{
			SDL_Rect rect{ x, 0, 1, 1 };
			const uint8 value = (x / 2) % 2 ? 255 : 0;
			SDL_FillSurfaceRect(surface, &rect, SDL_MapSurfaceRGBA(surface, value, value, value, 255));
		}
		FP_EXPECT(SDL_SaveBMP(surface, Paths::Content("Stripes.bmp").CStr()));
		SDL_DestroySurface(surface);
		auto* material = MaterialLibrary::Get().Create("Stripes.material");
		material->GetData().Shader = "Unlit";
		material->GetData().BaseColorMap = "Stripes.bmp";
		FP_EXPECT(material->Save());
		StrongObjectPtr<World> world(NewObject<World>());
		const Entity cube = world->CreateEntity("Foreground");
		world->Get<TransformComponent>(cube).Position = { 0, 0, -2 };
		world->Get<TransformComponent>(cube).Scale = { 8, 8, 0.1f };
		world->Add<MeshComponent>(cube).Mesh = "Cube";
		world->Add<MaterialComponent>(cube).Material = "Stripes.material";
		Renderer renderer(*device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
		FP_EXPECT(renderer.IsValid());
		constexpr uint32 width = 1281, height = 721;
		auto target = device->CreateRenderTarget(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, width, height, false);
		RenderView view;
		view.Width = width; view.Height = height; view.Orthographic = false;
		view.Projection = Mat4::Perspective(60, float(width) / height, 0.1f, 10000);
		view.Tonemap = false;
		auto render = [&] {
			auto* cmd = SDL_AcquireGPUCommandBuffer(device->GetHandle());
			renderer.Render(cmd, target->GetHandle(), width, height, *world.Get(), view);
			SDL_SubmitGPUCommandBuffer(cmd);
			Array<uint8> pixels;
			pixels.Resize(width * height * 4);
			FP_EXPECT(device->ReadPixels(target->GetHandle(), width, height, pixels.Data()));
			return pixels;
		};
		const auto baseline = render();
		const Entity hole = world->CreateEntity("Distant lens");
		world->Get<TransformComponent>(hole).Position = { 0, 0, -5000 };
		world->Add<BlackHoleComponent>(hole).StarBrightness = 0;
		const auto lensed = render();
		int maximumError = 0;
		for (uint32 x = width / 4; x < 3 * width / 4; ++x)
		{
			const uint32 offset = ((height / 2) * width + x) * 4;
			maximumError = std::max(maximumError, std::abs(int(baseline[offset]) - int(lensed[offset])));
		}
		FP_EXPECT(maximumError <= 2);
		MaterialLibrary::Get().Clear();
	}
	delete device;
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
