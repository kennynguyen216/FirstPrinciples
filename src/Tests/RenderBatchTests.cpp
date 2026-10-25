#include "Tests/TestFramework.h"
#include "Tests/TestProject.h"
#include "Engine/Engine.h"
#include "Engine/Material.h"
#include "Engine/Render/Renderer.h"
#include <SDL3/SDL.h>

using namespace FP;

namespace
{
	Array<uint8> RenderPixels(GPUDevice& device, Renderer& renderer, World& world, const RenderView& view, bool batching)
	{
		renderer.SetBatchingEnabled(batching);
		auto target = device.CreateRenderTarget(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, view.Width, view.Height, false);
		auto* cmd = SDL_AcquireGPUCommandBuffer(device.GetHandle());
		renderer.Render(cmd, target->GetHandle(), view.Width, view.Height, world, view);
		SDL_SubmitGPUCommandBuffer(cmd);
		Array<uint8> pixels;
		pixels.Resize(view.Width * view.Height * 4);
		FP_EXPECT(device.ReadPixels(target->GetHandle(), view.Width, view.Height, pixels.Data()));
		return pixels;
	}

	bool SamePixels(const Array<uint8>& a, const Array<uint8>& b)
	{
		return a.Num() == b.Num() && std::memcmp(a.Data(), b.Data(), a.Num()) == 0;
	}

	RenderView BatchView()
	{
		RenderView view;
		view.Width = 160;
		view.Height = 120;
		view.Projection = Mat4::Perspective(60, 4.0f / 3, 0.1f, 50);
		view.Mode = ViewMode::Lit;
		view.Orthographic = false;
		view.Tonemap = false;
		return view;
	}
}

FP_TEST(RenderBatch_SharedMeshesPreservePixelsAndMeshletVisibility)
{
	if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return;
	auto* device = GPUDevice::Create();
	if (!device) { SDL_QuitSubSystem(SDL_INIT_VIDEO); return; }
	{
		Engine::RegisterEngineComponents();
		StrongObjectPtr<World> world(NewObject<World>());
		Array<Entity> cubes;
		for (int32 i = 0; i < 12; ++i)
		{
			const Entity entity = world->CreateEntity("Instance");
			world->Add<MeshComponent>(entity).Mesh = "Cube";
			auto& transform = world->Get<TransformComponent>(entity);
			transform.Position = { float(i % 4) * 1.1f - 1.6f, float(i / 4) * 0.8f - 0.8f, -5 - i * 0.03f };
			transform.Scale = { 0.4f, 0.5f, 0.3f };
			transform.Rotation = { float(i * 3), float(i * 7), 0 };
			cubes.Add(entity);
		}
		Renderer renderer(*device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
		FP_EXPECT(renderer.IsValid());
		if (renderer.IsValid())
		{
			for (int32 frame = 0; frame < 5; ++frame)
			{
				auto view = BatchView();
				if (frame == 2) view.Mode = ViewMode::Wireframe;
				if (frame == 3) view.Mode = ViewMode::Unlit;
				if (frame == 4) { view.Width = 193; view.Height = 127; }
				world->Get<TransformComponent>(cubes[0]).Position.X = frame == 1 ? 100 : -1.6f;
				renderer.GetMeshletSettings().Enabled = false;
				const auto full = RenderPixels(*device, renderer, *world.Get(), view, false);
				FP_EXPECT(renderer.GetRenderStats().SceneDrawCalls == 12);
				const auto batched = RenderPixels(*device, renderer, *world.Get(), view, true);
				FP_EXPECT(SamePixels(full, batched));
				FP_EXPECT(renderer.GetRenderStats().SceneDrawCalls == 1);
				FP_EXPECT(renderer.GetRenderStats().InstancedDrawCalls == 1);
				FP_EXPECT(renderer.GetRenderStats().SceneInstances == 12);
				if (renderer.HasMeshletCulling())
				{
					renderer.GetMeshletSettings().Enabled = true;
					const auto culled = RenderPixels(*device, renderer, *world.Get(), view, false);
					const auto indirect = RenderPixels(*device, renderer, *world.Get(), view, true);
					FP_EXPECT(SamePixels(full, culled));
					FP_EXPECT(SamePixels(culled, indirect));
					FP_EXPECT(renderer.GetRenderStats().SceneDrawCalls == 1);
					FP_EXPECT(renderer.GetRenderStats().IndirectCommands >= 12);
					FP_EXPECT(renderer.GetRenderStats().OcclusionDrawCalls == (frame == 2 ? 0 : 1));
				}
			}
		}
	}
	delete device;
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

FP_TEST(RenderBatch_SpriteTintLayersAndTextureChangesKeepBlendOrder)
{
	if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return;
	auto* device = GPUDevice::Create();
	if (!device) { SDL_QuitSubSystem(SDL_INIT_VIDEO); return; }
	{
		Test::ScopedProject project;
		for (const char* name : { "BatchA.bmp", "BatchB.bmp" })
		{
			auto* surface = SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGBA32);
			SDL_FillSurfaceRect(surface, nullptr, SDL_MapSurfaceRGBA(surface, name[5] == 'A' ? 255 : 30, 80, 255, 255));
			const String path = Paths::Content(name);
			FP_EXPECT(SDL_SaveBMP(surface, path.CStr()));
			SDL_DestroySurface(surface);
		}
		Engine::RegisterEngineComponents();
		StrongObjectPtr<World> world(NewObject<World>());
		for (int32 i = 0; i < 6; ++i)
		{
			const Entity entity = world->CreateEntity("Sprite");
			auto& transform = world->Get<TransformComponent>(entity);
			transform.Position = { i * 0.1f, 0, -3 + i * 0.05f };
			transform.Rotation.Z = i * 13;
			auto& sprite = world->Add<SpriteComponent>(entity);
			sprite.Size = { 1 + i * 0.1f, 1 };
			sprite.Tint = { i / 6.0f, 1 - i / 6.0f, 0.3f, 0.4f };
			sprite.Layer = i < 3 ? 0 : 1;
			sprite.Texture = i == 4 ? "BatchB.bmp" : "BatchA.bmp";
		}
		Renderer renderer(*device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
		FP_EXPECT(renderer.IsValid());
		if (renderer.IsValid())
		{
			const auto full = RenderPixels(*device, renderer, *world.Get(), BatchView(), false);
			const auto batched = RenderPixels(*device, renderer, *world.Get(), BatchView(), true);
			FP_EXPECT(SamePixels(full, batched));
			FP_EXPECT(renderer.GetRenderStats().SceneDrawCalls == 4);
			FP_EXPECT(renderer.GetRenderStats().InstancedDrawCalls == 1);
			FP_EXPECT(renderer.GetRenderStats().SceneInstances == 6);
		}
	}
	delete device;
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

FP_TEST(RenderBatch_MaterialBoundariesAndGlassKeepOrderedDraws)
{
	if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return;
	auto* device = GPUDevice::Create();
	if (!device) { SDL_QuitSubSystem(SDL_INIT_VIDEO); return; }
	{
		Test::ScopedProject project;
		Engine::RegisterEngineComponents();
		auto& library = MaterialLibrary::Get();
		Material* red = library.Create("BatchRed.material");
		Material* glass = library.Create("BatchGlass.material");
		FP_EXPECT(red && glass);
		if (red && glass)
		{
			red->GetData().BaseColor = { 1, 0.1f, 0.2f, 1 };
			glass->GetData().Shader = "Glass";
			glass->GetData().BaseColor = { 0.1f, 0.8f, 1, 0.5f };
			StrongObjectPtr<World> world(NewObject<World>());
			for (int32 i = 0; i < 8; ++i)
			{
				const Entity entity = world->CreateEntity("Material instance");
				world->Add<MeshComponent>(entity).Mesh = "Cube";
				world->Get<TransformComponent>(entity).Position = { (i % 2) * 0.5f, 0, -6 + i * 0.4f };
				world->Get<TransformComponent>(entity).Scale = { 0.7f, 0.7f, 0.2f };
				world->Add<MaterialComponent>(entity).Material = i < 4 ? "BatchRed.material" : "BatchGlass.material";
			}
			Renderer renderer(*device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
			FP_EXPECT(renderer.IsValid());
			if (renderer.IsValid())
			{
				renderer.GetMeshletSettings().Enabled = false;
				const auto full = RenderPixels(*device, renderer, *world.Get(), BatchView(), false);
				const auto batched = RenderPixels(*device, renderer, *world.Get(), BatchView(), true);
				FP_EXPECT(SamePixels(full, batched));
				FP_EXPECT(renderer.GetRenderStats().SceneDrawCalls == 2);
				FP_EXPECT(renderer.GetRenderStats().InstancedDrawCalls == 2);
			}
		}
		library.Clear();
	}
	delete device;
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

FP_TEST(RenderBatch_ShadowMapsReuseInstancesForAllLightTypes)
{
	if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return;
	auto* device = GPUDevice::Create();
	if (!device) { SDL_QuitSubSystem(SDL_INIT_VIDEO); return; }
	{
		Engine::RegisterEngineComponents();
		StrongObjectPtr<World> world(NewObject<World>());
		for (int32 i = 0; i < 12; ++i)
		{
			const Entity entity = world->CreateEntity("Shadow instance");
			world->Add<MeshComponent>(entity).Mesh = "Cube";
			auto& transform = world->Get<TransformComponent>(entity);
			transform.Position = { (i % 4) * 0.8f - 1.2f, (i / 4) * 0.6f - 0.6f, -5 - i * 0.03f };
			transform.Scale = { 0.4f, 0.5f, 0.3f };
		}
		const Entity sun = world->CreateEntity("Sun");
		world->Add<DirectionalLightComponent>(sun).ShadowResolution = 64;
		world->Get<TransformComponent>(sun).Rotation = { -35, 20, 0 };
		const Entity spot = world->CreateEntity("Spot");
		world->Add<SpotLightComponent>(spot).ShadowResolution = 64;
		const Entity point = world->CreateEntity("Point");
		world->Add<PointLightComponent>(point).ShadowResolution = 64;
		Renderer renderer(*device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
		FP_EXPECT(renderer.IsValid());
		if (renderer.IsValid())
		{
			const auto full = RenderPixels(*device, renderer, *world.Get(), BatchView(), false);
			FP_EXPECT(renderer.GetRenderStats().ShadowDrawCalls == 96);
			const auto batched = RenderPixels(*device, renderer, *world.Get(), BatchView(), true);
			FP_EXPECT(SamePixels(full, batched));
			FP_EXPECT(renderer.GetRenderStats().ShadowDrawCalls == 8);
			FP_EXPECT(renderer.GetRenderStats().OcclusionDrawCalls == 1);
		}
	}
	delete device;
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

FP_TEST(RenderBatch_InstanceUploadsSurviveMultipleViewsInOneSubmission)
{
	if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return;
	auto* device = GPUDevice::Create();
	if (!device) { SDL_QuitSubSystem(SDL_INIT_VIDEO); return; }
	{
		Engine::RegisterEngineComponents();
		StrongObjectPtr<World> world(NewObject<World>());
		const Entity entity = world->CreateEntity("Moving instance");
		world->Add<MeshComponent>(entity).Mesh = "Cube";
		Renderer renderer(*device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
		FP_EXPECT(renderer.IsValid());
		if (renderer.IsValid())
		{
			renderer.GetMeshletSettings().Enabled = false;
			auto view = BatchView();
			world->Get<TransformComponent>(entity).Position = { -1, 0, -4 };
			const auto first = RenderPixels(*device, renderer, *world.Get(), view, false);
			world->Get<TransformComponent>(entity).Position.X = 1;
			const auto second = RenderPixels(*device, renderer, *world.Get(), view, false);
			FP_EXPECT(!SamePixels(first, second));
			auto a = device->CreateRenderTarget(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, view.Width, view.Height, false);
			auto b = device->CreateRenderTarget(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, view.Width, view.Height, false);
			renderer.SetBatchingEnabled(true);
			auto* cmd = SDL_AcquireGPUCommandBuffer(device->GetHandle());
			world->Get<TransformComponent>(entity).Position.X = -1;
			renderer.Render(cmd, a->GetHandle(), view.Width, view.Height, *world.Get(), view);
			world->Get<TransformComponent>(entity).Position.X = 1;
			renderer.Render(cmd, b->GetHandle(), view.Width, view.Height, *world.Get(), view);
			SDL_SubmitGPUCommandBuffer(cmd);
			Array<uint8> actual;
			actual.Resize(first.Num());
			FP_EXPECT(device->ReadPixels(a->GetHandle(), view.Width, view.Height, actual.Data()));
			FP_EXPECT(SamePixels(first, actual));
			FP_EXPECT(device->ReadPixels(b->GetHandle(), view.Width, view.Height, actual.Data()));
			FP_EXPECT(SamePixels(second, actual));
		}
	}
	delete device;
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

FP_TEST(RenderBatch_AnimatedPosesKeepIndependentDrawsAlongsideInstances)
{
	if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return;
	auto* device = GPUDevice::Create();
	if (!device) { SDL_QuitSubSystem(SDL_INIT_VIDEO); return; }
	{
		Engine::RegisterEngineComponents();
		StrongObjectPtr<World> world(NewObject<World>());
		for (int32 i = 0; i < 2; ++i)
		{
			const Entity cube = world->CreateEntity("Static");
			world->Add<MeshComponent>(cube).Mesh = "Cube";
			world->Get<TransformComponent>(cube).Position = { i * 1.2f - 0.6f, -0.6f, -4 - i * 0.1f };
			world->Get<TransformComponent>(cube).Scale = { 0.3f, 0.3f, 0.3f };
			const Entity noodle = world->CreateEntity("Animated");
			world->Add<SkeletalMeshComponent>(noodle).Mesh = "Characters/Noodle.gltf";
			world->Get<TransformComponent>(noodle).Position = { i * 1.4f - 0.7f, -0.8f, -6 - i * 0.1f };
			auto& skin = world->Add<SkeletalPoseComponent>(noodle).Skin;
			skin.Resize(5);
			skin[4] = Mat4::Translation({ i * 0.4f, 0.2f, 0 });
		}
		const Entity sprite = world->CreateEntity("Overlay");
		world->Add<SpriteComponent>(sprite).Tint = { 0.2f, 0.8f, 0.4f, 0.3f };
		world->Get<TransformComponent>(sprite).Position = { 0, 0, -3 };
		const Entity light = world->CreateEntity("Sun");
		world->Add<DirectionalLightComponent>(light).ShadowResolution = 64;
		Renderer renderer(*device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
		FP_EXPECT(renderer.IsValid());
		if (renderer.IsValid())
		{
			renderer.GetMeshletSettings().Enabled = false;
			for (ViewMode mode : { ViewMode::Lit, ViewMode::Unlit, ViewMode::Wireframe })
			{
				auto view = BatchView();
				view.Mode = mode;
				const auto full = RenderPixels(*device, renderer, *world.Get(), view, false);
				FP_EXPECT(renderer.GetRenderStats().SceneDrawCalls == 7);
				const auto batched = RenderPixels(*device, renderer, *world.Get(), view, true);
				FP_EXPECT(SamePixels(full, batched));
				FP_EXPECT(renderer.GetRenderStats().SceneDrawCalls == 6);
				FP_EXPECT(renderer.GetRenderStats().SceneInstances == 7);
				if (mode == ViewMode::Lit) FP_EXPECT(renderer.GetRenderStats().ShadowDrawCalls == 5);
			}
		}
	}
	delete device;
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
