#include "Tests/TestFramework.h"
#include "Engine/Engine.h"
#include "Engine/Paths.h"
#include "Engine/Render/EnvironmentMap.h"
#include "Engine/Render/Renderer.h"
#include "Engine/SceneSerializer.h"
#include "Core/Misc/File.h"

#include <SDL3/SDL.h>

#include <filesystem>
#include <chrono>
#include <limits>

using namespace FP;

FP_TEST(Environment_ConstantRadianceSurvivesFiltering)
{
	Array<Vec4> pixels;
	for (int32 i = 0; i < 32 * 16; ++i)
		pixels.Add({ 2, 3, 4, 1 });
	EnvironmentMap map;
	FP_EXPECT(map.Build(pixels, 32, 16));
	for (Vec3 direction : { Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, -1) })
	{
		FP_EXPECT((map.Irradiance(direction) - Vec3(2, 3, 4)).Length() < 0.04f);
		for (int32 level = 0; level < map.Levels; ++level)
			FP_EXPECT((map.Sample(direction, level) - Vec3(2, 3, 4)).Length() < 0.0001f);
	}
}

FP_TEST(Environment_LightingFollowsTheBrightHemisphere)
{
	Array<Vec4> pixels;
	for (int32 y = 0; y < 16; ++y)
		for (int32 x = 0; x < 32; ++x)
			pixels.Add(y < 8 ? Vec4(4, 1, 0.5f, 1) : Vec4(0.1f, 0.1f, 0.1f, 1));
	EnvironmentMap map;
	FP_EXPECT(map.Build(pixels, 32, 16));
	FP_EXPECT(map.Irradiance({ 0, 1, 0 }).X > map.Irradiance({ 0, -1, 0 }).X * 5);
	FP_EXPECT((map.Sample({ -1, 0, 0.00001f }) - map.Sample({ -1, 0, -0.00001f })).Length() < 0.001f);
}

FP_TEST(Environment_RejectsInvalidPanoramas)
{
	EnvironmentMap map;
	Array<Vec4> pixels;
	for (int32 i = 0; i < 32; ++i)
		pixels.Add({ 1, 1, 1, 1 });
	FP_EXPECT(!map.Build(pixels, 4, 8));
	FP_EXPECT(!map.Build(pixels, 8, 8));
	pixels[0].X = std::numeric_limits<float>::quiet_NaN();
	FP_EXPECT(!map.Build(pixels, 8, 4));
	pixels[0].X = -1;
	FP_EXPECT(!map.Build(pixels, 8, 4));
	FP_EXPECT(!map.Load("MissingEnvironment.hdr"));
	FP_EXPECT(map.Levels == 0 && map.Pixels.IsEmpty());
}

FP_TEST(Environment_DownsamplingPreservesSmallBrightFeatures)
{
	Array<Vec4> pixels;
	pixels.Resize(1024 * 512);
	pixels[256 * 1024] = { 16, 8, 4, 1 };
	EnvironmentMap map;
	FP_EXPECT(map.Build(pixels, 1024, 512));
	FP_EXPECT(map.Width == 512 && map.Height == 256);
	const Vec3 light = map.Pixels[128 * 512].XYZ();
	FP_EXPECT(light.X > 3.9f && light.X < 4.1f);
	FP_EXPECT(light.Y > 1.9f && light.Y < 2.1f);
}

FP_TEST(Environment_SceneRoundTripAndValidation)
{
	Engine::RegisterEngineComponents();
	World world;
	const Entity entity = world.CreateEntity("Sky");
	auto& sky = world.Add<SkyboxComponent>(entity);
	sky.Texture = "Environments/Studio.hdr";
	sky.Intensity = 2.5f;
	sky.Rotation = 135;
	sky.Visible = false;
	World loaded;
	FP_EXPECT(SceneSerializer::LoadFromString(loaded, SceneSerializer::SaveToString(world).View()));
	const auto& copy = loaded.Get<SkyboxComponent>(loaded.FindEntityByName("Sky"));
	FP_EXPECT(copy.Texture == sky.Texture && copy.Intensity == 2.5f && copy.Rotation == 135 && !copy.Visible && copy.Enabled);
	sky.Intensity = -1;
	FP_EXPECT(!SceneSerializer::Validate(world));
}

FP_TEST(Environment_GPUControlsBackgroundAndLighting)
{
	if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
		return;
	GPUDevice* device = GPUDevice::Create();
	if (!device)
	{
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
		return;
	}
	Engine::RegisterEngineComponents();
	namespace fs = std::filesystem;
	const fs::path path = fs::temp_directory_path() / (std::string("FPEnvironment_") + Guid::New().ToString().CStr() + ".hdr");
	String image = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 4 +X 8\n";
	const char pixel[4] = { 64, static_cast<char>(128), static_cast<char>(192), static_cast<char>(129) };
	for (int32 i = 0; i < 32; ++i)
		image += std::string_view(pixel, 4);
	FP_EXPECT(File::WriteAllText(path.generic_string().c_str(), image.View()));
	{
		Renderer renderer(*device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB);
		FP_EXPECT(renderer.IsValid());
		RefPtr<GPUTexture> target = device->CreateRenderTarget(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB, 32, 32, false);
		World world;
		const Entity skyEntity = world.CreateEntity("Sky");
		auto& sky = world.Add<SkyboxComponent>(skyEntity);
		sky.Texture = fs::relative(path, Paths::GetContentDir().CStr()).generic_string().c_str();
		sky.Enabled = false;
		TransformComponent camera;
		camera.Position = { 0, 0, 3 };
		CameraComponent lens;
		lens.Perspective = true;
		lens.Background = { 0.1f, 0, 0, 1 };
		lens.Tonemap = false;
		const RenderView view = RenderView::FromCamera(camera, lens, 32, 32);
		uint8 read[32 * 32 * 4] = {};
		auto render = [&]() {
			SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device->GetHandle());
			renderer.Render(cmd, target->GetHandle(), 32, 32, world, view);
			FP_EXPECT(SDL_SubmitGPUCommandBuffer(cmd));
			FP_EXPECT(device->ReadPixels(target->GetHandle(), 32, 32, read));
		};
		render();
		const uint8 background = read[0];
		FP_EXPECT(background > 0 && read[1] == 0 && read[2] == 0);
		sky.Enabled = true;
		render();
		FP_EXPECT(read[1] > 100 && read[2] > 100);
		const Entity sphere = world.CreateEntity("Sphere");
		world.Add<MeshComponent>(sphere).Mesh = "Sphere";
		sky.Visible = false;
		render();
		const int32 center = (16 * 32 + 16) * 4;
		FP_EXPECT(read[0] == background && read[1] == 0 && read[2] == 0);
		FP_EXPECT(read[center + 1] > 100 && read[center + 2] > 100);
		sky.Intensity = 0;
		render();
		FP_EXPECT(read[center] == 0 && read[center + 1] == 0 && read[center + 2] == 0);
		sky.Texture = "MissingEnvironment.hdr";
		sky.Visible = true;
		sky.Intensity = 1;
		render();
		FP_EXPECT(read[0] == background && read[1] == 0 && read[2] == 0);

		// A two color panorama makes rotation and file reloads visible.
		image = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 4 +X 8\n";
		for (int32 y = 0; y < 4; ++y)
			for (int32 x = 0; x < 8; ++x)
			{
				const char split[4] = { static_cast<char>(x < 4 ? 192 : 0), 0,
					static_cast<char>(x < 4 ? 0 : 192), static_cast<char>(129) };
				image += std::string_view(split, 4);
			}
		FP_EXPECT(File::WriteAllText(path.generic_string().c_str(), image.View()));
		sky.Texture = fs::relative(path, Paths::GetContentDir().CStr()).generic_string().c_str();
		sky.Visible = false;
		render();
		FP_EXPECT(read[center + 2] > read[center]);
		sky.Rotation = 180;
		render();
		FP_EXPECT(read[center] > read[center + 2]);

		world.DestroyEntity(sphere);
		sky.Visible = true;
		sky.Rotation = 0;
		render();
		FP_EXPECT(read[center] > read[center + 2]);
		const uint8 red = read[center];
		camera.Position = { 10, 4, -8 };
		const RenderView translated = RenderView::FromCamera(camera, lens, 32, 32);
		SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device->GetHandle());
		renderer.Render(cmd, target->GetHandle(), 32, 32, world, translated);
		FP_EXPECT(SDL_SubmitGPUCommandBuffer(cmd));
		FP_EXPECT(device->ReadPixels(target->GetHandle(), 32, 32, read));
		FP_EXPECT(read[center] == red && read[center + 2] == 0);

		// Replacing the active file refreshes lighting on the next frame.
		FP_EXPECT(File::WriteAllText(path.generic_string().c_str(), "invalid panorama"));
		std::error_code ec;
		fs::last_write_time(path, fs::file_time_type::clock::now() + std::chrono::seconds(1), ec);
		render();
		FP_EXPECT(read[0] == background && read[1] == 0 && read[2] == 0);
	}
	std::error_code ec;
	fs::remove(path, ec);
	delete device;
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
