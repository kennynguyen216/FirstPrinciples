// Standalone player. Opens a window and runs a scene with the game's systems.
// Usage: Runtime [scene relative to Content] [--screenshot out.bmp] [--host [port]] [--join address:port]
//                [--password text]
// --screenshot saves a frame after half a second and quits, handy as a smoke test.
// --host starts a peer to peer session on a port (7777 by default) that others can join.
// --join connects to a peer already in a session, for example --join 192.168.1.20:7777.
// --password lets in only peers started with the same one. Traffic is encrypted either way.

#include "Engine/Engine.h"
#include "Engine/Net/NetworkSystem.h"
#include "Engine/Animation/AnimationSystem.h"
#include "Engine/Audio/AudioDevice.h"
#include "Engine/Project.h"
#include "Core/Jobs/JobSystem.h"
#include "Engine/Render/Renderer.h"
#include "Engine/SceneSerializer.h"
#include "Engine/Scripting/ScriptRuntime.h"
#include "Engine/Scripting/ScriptSystem.h"
#include "Game.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

using namespace FP;

namespace
{
	// Renders one frame offscreen and writes it as a BMP.
	void SaveScreenshot(GPUDevice& device, Renderer& renderer, SDL_GPUTextureFormat format, World& world, uint32 width, uint32 height, const char* path)
	{
		RefPtr<GPUTexture> target = device.CreateRenderTarget(format, width, height, false);
		SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device.GetHandle());
		renderer.Render(cmd, target->GetHandle(), width, height, world, RenderView::FromWorld(world, width, height));
		SDL_SubmitGPUCommandBuffer(cmd);

		Array<uint8> pixels;
		pixels.Resize(static_cast<int32>(width * height * 4));
		if (!device.ReadPixels(target->GetHandle(), width, height, pixels.Data()))
			return;
		const bool bgra = format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM || format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB;
		SDL_Surface* surface = SDL_CreateSurfaceFrom(static_cast<int>(width), static_cast<int>(height),
			bgra ? SDL_PIXELFORMAT_ARGB8888 : SDL_PIXELFORMAT_ABGR8888, pixels.Data(), static_cast<int>(width * 4));
		SDL_SaveBMP(surface, path);
		SDL_DestroySurface(surface);
		FP_LOG("Saved screenshot %s", path);
	}
}

int main(int argc, char** argv)
{
	Log::OpenFile((Paths::GetProjectDir() + "/Saved/Logs/Runtime.log").CStr());

	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		FP_LOG_ERROR("SDL_Init failed: %s", SDL_GetError());
		return 1;
	}

	JobSystem::Initialize();

	String scene = Paths::GetProject().StartScene;
	const char* screenshotPath = nullptr;
	bool host = false;
	uint16 hostPort = 7777;
	const char* joinAddress = nullptr;
	const char* password = "";
	for (int i = 1; i < argc; ++i)
	{
		const String arg = argv[i];
		if (arg == "--screenshot" && i + 1 < argc)
			screenshotPath = argv[++i];
		else if (arg == "--host")
		{
			host = true;
			int64 port = 0;
			if (i + 1 < argc && String(argv[i + 1]).ToInt(port) && port > 0 && port < 65536)
			{
				hostPort = static_cast<uint16>(port);
				++i;
			}
		}
		else if (arg == "--join" && i + 1 < argc)
			joinAddress = argv[++i];
		else if (arg == "--password" && i + 1 < argc)
			password = argv[++i];
		else
			scene = argv[i];
	}

	GPUDevice* device = GPUDevice::Create();
	SDL_Window* window = device ? SDL_CreateWindow(Paths::GetProject().Name.CStr(), 1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY) : nullptr;
	if (!window || !SDL_ClaimWindowForGPUDevice(device->GetHandle(), window))
	{
		FP_LOG_ERROR("Could not create a GPU window: %s", SDL_GetError());
		return 1;
	}

	// Linear swapchain so the GPU applies sRGB encoding for us.
	if (SDL_WindowSupportsGPUSwapchainComposition(device->GetHandle(), window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR))
		SDL_SetGPUSwapchainParameters(device->GetHandle(), window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR, SDL_GPU_PRESENTMODE_VSYNC);
	else
		FP_LOG_WARN("sRGB swapchain not supported. Colors will look dark.");
	const SDL_GPUTextureFormat swapchainFormat = SDL_GetGPUSwapchainTextureFormat(device->GetHandle(), window);

	Engine::RegisterEngineComponents();
	Game::RegisterComponents();

	AudioDevice audio;
	if (const AudioResult initialized = audio.Initialize(); !initialized)
		FP_LOG_WARN("Audio unavailable: %s", initialized.Message.CStr());

	StrongObjectPtr<World> world(NewObject<World>(nullptr, "GameWorld"));
	world->GetAudio().Attach(&audio);
	const String scenePath = Paths::Content(scene);
	if (!SceneSerializer::LoadFromFile(*world.Get(), scenePath.CStr()))
		FP_LOG_WARN("Starting with an empty world");

	// Networking first so remote entities are in place before the game's systems look at them.
	if (host || joinAddress)
	{
		NetSession& session = world->AddSystem<NetworkSystem>()->GetSession();
		session.SetPassword(password);
		// Joiners listen too, on any free port, since every peer talks to every other one.
		if (!session.Start(host ? hostPort : 0))
			return 1;
		NetAddress address;
		if (joinAddress)
		{
			if (!NetAddress::Parse(joinAddress, address, 7777))
			{
				FP_LOG_ERROR("Could not read the address %s", joinAddress);
				return 1;
			}
			session.Join(address);
		}
	}

	// Scripts are optional. The game still runs its built in systems without them.
	ScriptRuntime::Get().LoadModule(ScriptRuntime::GetDefaultModulePath());
	world->AddSystem<ScriptSystem>();
	world->AddSystem<AnimationSystem>();
	Game::AddSystems(*world.Get());
	world->BeginPlay();

	{
		Renderer renderer(*device, swapchainFormat);
		renderer.PreloadTextures(*world.Get());
		uint64 lastTicks = SDL_GetTicksNS();
		int32 frame = 0;
		bool running = renderer.IsValid();
		while (running)
		{
			SDL_Event event;
			while (SDL_PollEvent(&event))
			{
				audio.ProcessEvent(event);
				if (event.type == SDL_EVENT_QUIT)
					running = false;
				Input::ProcessEvent(event);
			}

			const uint64 now = SDL_GetTicksNS();
			float deltaTime = static_cast<float>(now - lastTicks) / 1e9f;
			lastTicks = now;
			if (deltaTime > 0.1f)
				deltaTime = 0.1f; // avoid huge steps after a stall

			world->Update(deltaTime);
			audio.Update();
			Input::EndFrame();

			SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device->GetHandle());
			SDL_GPUTexture* swapchain = nullptr;
			uint32 width = 0;
			uint32 height = 0;
			if (SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window, &swapchain, &width, &height) && swapchain)
				renderer.Render(cmd, swapchain, width, height, *world.Get(), RenderView::FromWorld(*world.Get(), width, height));
			SDL_SubmitGPUCommandBuffer(cmd);

			if (screenshotPath && world->GetTime() > 0.5 && width > 0)
			{
				SaveScreenshot(*device, renderer, swapchainFormat, *world.Get(), width, height, screenshotPath);
				running = false;
			}

			// Collecting once a second keeps GC pauses rare and predictable.
			if (++frame % 60 == 0)
				GarbageCollector::Get().Collect();
		}
		world->EndPlay();
	}

	world.Reset();
	GarbageCollector::Get().Collect();
	audio.Shutdown();
	ScriptRuntime::Get().UnloadModule();
	SDL_ReleaseWindowFromGPUDevice(device->GetHandle(), window);
	SDL_DestroyWindow(window);
	delete device;
	JobSystem::Shutdown();
	SDL_Quit();
	Log::Flush();
	return 0;
}
