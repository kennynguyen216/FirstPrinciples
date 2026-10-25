#include "Tests/TestFramework.h"
#include "Engine/Render/MeshletCulling.h"
#include "Engine/Render/Renderer.h"
#include "Engine/Engine.h"
#include <SDL3/SDL.h>

using namespace FP;
namespace
{
	Array<SDL_GPUIndexedIndirectDrawCommand> ReadCommands(GPUDevice& device, SDL_GPUBuffer* buffer, uint32 count)
	{
		Array<SDL_GPUIndexedIndirectDrawCommand> result;
		result.Resize(static_cast<int32>(count));
		SDL_GPUTransferBufferCreateInfo info{};
		info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD; info.size = count * sizeof(SDL_GPUIndexedIndirectDrawCommand);
		auto* transfer = SDL_CreateGPUTransferBuffer(device.GetHandle(), &info);
		auto* cmd = SDL_AcquireGPUCommandBuffer(device.GetHandle());
		auto* pass = SDL_BeginGPUCopyPass(cmd);
		SDL_GPUBufferRegion source{ buffer, 0, info.size }; SDL_GPUTransferBufferLocation dest{ transfer, 0 };
		SDL_DownloadFromGPUBuffer(pass, &source, &dest); SDL_EndGPUCopyPass(pass);
		auto* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
		SDL_WaitForGPUFences(device.GetHandle(), true, &fence, 1);
		std::memcpy(result.Data(), SDL_MapGPUTransferBuffer(device.GetHandle(), transfer, false), info.size);
		SDL_UnmapGPUTransferBuffer(device.GetHandle(), transfer);
		SDL_ReleaseGPUFence(device.GetHandle(), fence); SDL_ReleaseGPUTransferBuffer(device.GetHandle(), transfer);
		return result;
	}
}
FP_TEST(Meshlet_GPUOcclusionFrustumAndConservativeEdges)
{
	if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return;
	auto* device = GPUDevice::Create();
	if (!device) { SDL_QuitSubSystem(SDL_INIT_VIDEO); return; }
	{
		MeshletCulling culling(*device);
		FP_EXPECT(culling.IsValid());
		if (culling.IsValid())
		{
			// Odd dimensions exercise far-depth padding of the last reduction tile.
			auto depth = device->CreateRenderTarget(device->GetDepthFormat(), 65, 65, true, true);
			Array<MeshletCullItem> items;
			items.Add({ {}, {-0.2f,-0.2f,0.6f,0}, {0.2f,0.2f,0.7f,0}, 0, 3, {} }); // occluded
			items.Add({ {}, {-0.2f,-0.2f,0.2f,0}, {0.2f,0.2f,0.25f,0}, 3, 6, {} }); // in front
			items.Add({ {}, {2,-0.2f,0.2f,0}, {3,0.2f,0.25f,0}, 9, 3, {} }); // outside
			items.Add({ {}, {-0.2f,-0.2f,-0.1f,0}, {0.2f,0.2f,0.7f,0}, 12, 3, {} }); // near-plane crossing
			items.Add({ {}, {0.95f,-0.2f,0.6f,0}, {1,0.2f,0.7f,0}, 15, 3, {} }); // far padded tile
			MeshletSettings settings;
			auto run = [&](float clearDepth) {
				auto* cmd = SDL_AcquireGPUCommandBuffer(device->GetHandle());
				SDL_GPUDepthStencilTargetInfo d{}; d.texture = depth->GetHandle(); d.clear_depth = clearDepth;
				d.load_op = SDL_GPU_LOADOP_CLEAR; d.store_op = SDL_GPU_STOREOP_STORE;
				d.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE; d.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
				auto* pass = SDL_BeginGPURenderPass(cmd, nullptr, 0, &d); SDL_EndGPURenderPass(pass);
				FP_EXPECT(culling.Dispatch(cmd, depth->GetHandle(), 65, 65, items, settings));
				SDL_SubmitGPUCommandBuffer(cmd);
				return ReadCommands(*device, culling.Commands(), static_cast<uint32>(items.Num()));
			};
			auto commands = run(0.3f);
			const uint32 expected[] = {0, 1, 0, 1, 1};
			for (int32 i = 0; i < items.Num(); ++i)
			{
				FP_EXPECT(commands[i].num_instances == expected[i]);
				FP_EXPECT(commands[i].num_indices == items[i].IndexCount && commands[i].first_index == items[i].FirstIndex);
				FP_EXPECT(commands[i].vertex_offset == 0 && commands[i].first_instance == 0);
			}
			culling.ReadStats(); SDL_WaitForGPUIdle(device->GetHandle());
			const auto stats = culling.ReadStats();
			FP_EXPECT(stats.Total == 5 && stats.Visible == 3 && stats.Frustum == 1 && stats.Occluded == 1);
			commands = run(1.0f); FP_EXPECT(commands[0].num_instances == 1); // moved occluder cannot leave stale rejection
			settings.Occlusion = false; commands = run(0.3f); FP_EXPECT(commands[0].num_instances == 1);
			settings.Frustum = false; commands = run(0.3f);
			for (const auto& command : commands) FP_EXPECT(command.num_instances == 1);
		}
	}
	delete device;
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

FP_TEST(Meshlet_RendererMatchesFullDrawsAfterMotionAndResize)
{
	if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return;
	auto* device = GPUDevice::Create();
	if (!device) { SDL_QuitSubSystem(SDL_INIT_VIDEO); return; }
	{
		Engine::RegisterEngineComponents();
		StrongObjectPtr<World> world(NewObject<World>());
		const Entity wall = world->CreateEntity("Occluder");
		world->Add<MeshComponent>(wall).Mesh = "Cube";
		world->Get<TransformComponent>(wall).Position = { 0, 0, -3 };
		world->Get<TransformComponent>(wall).Scale = { 3, 3, 0.5f };
		const Entity hidden = world->CreateEntity("Hidden sphere");
		world->Add<MeshComponent>(hidden).Mesh = "Sphere";
		world->Get<TransformComponent>(hidden).Position = { 0, 0, -5 };
		const Entity side = world->CreateEntity("Visible sphere");
		world->Add<MeshComponent>(side).Mesh = "Sphere";
		world->Get<TransformComponent>(side).Position = { 1.7f, 0.6f, -2.5f };
		Renderer renderer(*device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
		FP_EXPECT(renderer.IsValid() && renderer.HasMeshletCulling());
		if (renderer.IsValid() && renderer.HasMeshletCulling())
		{
			for (int frame = 0; frame < 4; ++frame)
			{
				const uint32 w = frame == 3 ? 193 : 160, h = frame == 3 ? 127 : 120;
				RenderView view;
				view.Orthographic = frame == 2;
				view.Projection = frame == 2 ? Mat4::Orthographic(-3, 3, -2, 2, 0.1f, 50) : Mat4::Perspective(60, float(w) / h, 0.1f, 50);
				view.Mode = ViewMode::Unlit;
				view.Width = w; view.Height = h;
				if (frame > 0) world->Get<TransformComponent>(wall).Position.X = 3;
				if (frame == 3) { view.View = Mat4::Translation({-0.3f, -0.4f, 0}); view.CameraPosition = {0.3f, 0.4f, 0}; }
				auto target = device->CreateRenderTarget(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, w, h, false);
				Array<uint8> full, culled; full.Resize(w * h * 4); culled.Resize(w * h * 4);
				auto render = [&](bool enabled, Array<uint8>& pixels) {
					renderer.GetMeshletSettings().Enabled = enabled;
					auto* cmd = SDL_AcquireGPUCommandBuffer(device->GetHandle());
					renderer.Render(cmd, target->GetHandle(), w, h, *world.Get(), view);
					SDL_SubmitGPUCommandBuffer(cmd);
					FP_EXPECT(device->ReadPixels(target->GetHandle(), w, h, pixels.Data()));
				};
				render(false, full); render(true, culled);
				FP_EXPECT(renderer.GetActiveMeshlets() > 0);
				FP_EXPECT(std::memcmp(full.Data(), culled.Data(), full.Num()) == 0);
				if (frame == 0)
				{
					renderer.GetMeshletStats(); SDL_WaitForGPUIdle(device->GetHandle());
					FP_EXPECT(renderer.GetMeshletStats().Occluded > 0);
				}
			}
		}
	}
	delete device;
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

FP_TEST(Meshlet_ObserverUsesSeparateLiveCullingCamera)
{
	if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return;
	auto* device = GPUDevice::Create();
	if (!device) { SDL_QuitSubSystem(SDL_INIT_VIDEO); return; }
	{
		Engine::RegisterEngineComponents();
		StrongObjectPtr<World> world(NewObject<World>());
		const Entity cube = world->CreateEntity("Outside camera, inside observer");
		world->Add<MeshComponent>(cube).Mesh = "Cube";
		world->Get<TransformComponent>(cube).Position = { 2.1f, 0, -3 };
		world->Get<TransformComponent>(cube).Scale = { 0.2f, 0.2f, 0.2f };
		Renderer renderer(*device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
		FP_EXPECT(renderer.IsValid() && renderer.HasMeshletCulling());
		if (renderer.IsValid() && renderer.HasMeshletCulling())
		{
			renderer.GetMeshletSettings().Occlusion = false;
			RenderView camera;
			camera.Orthographic = false;
			camera.Width = camera.Height = 128;
			camera.Projection = Mat4::Perspective(60, 1, 0.1f, 100);
			camera.Background = { 0, 0, 0, 1 }; camera.Mode = ViewMode::Unlit; camera.Tonemap = false;
			auto target = device->CreateRenderTarget(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, 128, 128, false);
			auto litPixels = [&](const RenderView* culling) {
				const RenderView observer = camera.Expanded(1.5f);
				auto* cmd = SDL_AcquireGPUCommandBuffer(device->GetHandle());
				renderer.Render(cmd, target->GetHandle(), 128, 128, *world.Get(), observer, nullptr, culling);
				SDL_SubmitGPUCommandBuffer(cmd);
				Array<uint8> pixels; pixels.Resize(128 * 128 * 4);
				FP_EXPECT(device->ReadPixels(target->GetHandle(), 128, 128, pixels.Data()));
				int lit = 0;
				for (int i = 0; i < pixels.Num(); i += 4) if (pixels[i] > 100) ++lit;
				return lit;
			};
			FP_EXPECT(litPixels(&camera) == 0);
			FP_EXPECT(litPixels(nullptr) > 0); // expanding the observer must not expand the culling camera
			camera.View = Mat4::Translation({ -1, 0, 0 }); camera.CameraPosition = { 1, 0, 0 };
			FP_EXPECT(litPixels(&camera) > 0); // live movement reveals the cluster without freezing
		}
	}
	delete device;
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
