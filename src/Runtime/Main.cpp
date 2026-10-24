// Standalone player. Opens a window and runs a scene with the game's systems.
// Usage: Runtime [scene relative to Content] [--screenshot out.bmp] [--host [port]] [--join address:port]
//                [--password text]
// --screenshot saves a frame after half a second and quits, handy as a smoke test.
// --wait seconds takes it later instead, for scenes that need time to play out.
// --host starts a peer to peer session on a port (7777 by default) that others can join.
// --join connects to a peer already in a session, for example --join 192.168.1.20:7777.
// --password lets in only peers started with the same one. Traffic is encrypted either way.

#include "Engine/Engine.h"
#include "Engine/Net/NetworkSystem.h"
#include "Engine/Animation/AnimationSystem.h"
#include "Engine/Physics/PhysicsSystem.h"
#include "Engine/UI/UISystem.h"
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

#include <cstdlib>

using namespace FP;

namespace
{
	// The game picks how the mouse behaves. It is only held while the window has focus.
	void ApplyCursorMode(SDL_Window* window)
	{
		const CursorMode mode = Input::GetCursorMode();
		const bool focused = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
		SDL_SetWindowRelativeMouseMode(window, focused && mode == CursorMode::Locked);
		if (mode == CursorMode::Visible)
			SDL_ShowCursor();
		else
			SDL_HideCursor();
	}

	// Renders one frame offscreen and writes it as a BMP.
	// The primary camera's view, with the window's pixel density so UI pages keep their size.
	RenderView GameView(World& world, SDL_Window* window, uint32 width, uint32 height)
	{
		RenderView view = RenderView::FromWorld(world, width, height);
		view.PixelScale = SDL_GetWindowPixelDensity(window);
		return view;
	}

	void SaveScreenshot(GPUDevice& device, Renderer& renderer, SDL_GPUTextureFormat format, World& world, SDL_Window* window, uint32 width, uint32 height, const char* path)
	{
		RefPtr<GPUTexture> target = device.CreateRenderTarget(format, width, height, false);
		SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device.GetHandle());
		renderer.Render(cmd, target->GetHandle(), width, height, world, GameView(world, window, width, height));
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
	double screenshotDelay = 0.5;
	bool host = false;
	bool voice = false;
	uint16 hostPort = 7777;
	const char* joinAddress = nullptr;
	const char* password = "";
	for (int i = 1; i < argc; ++i)
	{
		const String arg = argv[i];
		if (arg == "--screenshot" && i + 1 < argc)
			screenshotPath = argv[++i];
		else if (arg == "--wait" && i + 1 < argc)
			screenshotDelay = std::atof(argv[++i]);
		else if (arg == "--voice")
			voice = true;
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
	if (voice && !host && !joinAddress)
	{
		FP_LOG_ERROR("Voice chat needs --host or --join. Use a headset and hold V to talk.");
		return 1;
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
		NetworkSystem* network = world->AddSystem<NetworkSystem>();
		NetSession& session = network->GetSession();
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
		if (voice)
		{
			network->GetVoiceChat().SetEnabled(true);
			FP_LOG("Proximity voice enabled. Use a headset; hold V to talk. %s", network->GetVoiceChat().GetStatusText().CStr());
		}
	}

	// Scripts are optional. The game still runs its built in systems without them.
	ScriptRuntime::Get().LoadModule(ScriptRuntime::GetDefaultModulePath());
	world->AddSystem<ScriptSystem>();
	world->AddSystem<PhysicsSystem>();
	world->AddSystem<AnimationSystem>();
	world->AddSystem<UISystem>();
	Game::AddSystems(*world.Get());
	world->BeginPlay();

	{
		Renderer renderer(*device, swapchainFormat);
		renderer.PreloadTextures(*world.Get());
		uint64 lastTicks = SDL_GetTicksNS();
		int32 frame = 0;
		bool running = renderer.IsValid();
		// Typed text reaches Input so UI text boxes work.
		SDL_StartTextInput(window);
		while (running)
		{
			SDL_Event event;
			while (SDL_PollEvent(&event))
			{
				audio.ProcessEvent(event);
				if (event.type == SDL_EVENT_QUIT)
					running = false;
				// SDL reports the mouse in window points. The game view and UI work in pixels.
				const float density = SDL_GetWindowPixelDensity(window);
				if (event.type == SDL_EVENT_MOUSE_MOTION)
				{
					event.motion.x *= density;
					event.motion.y *= density;
					event.motion.xrel *= density;
					event.motion.yrel *= density;
				}
				Input::ProcessEvent(event);
			}

			ApplyCursorMode(window);

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
				renderer.Render(cmd, swapchain, width, height, *world.Get(), GameView(*world.Get(), window, width, height));
			SDL_SubmitGPUCommandBuffer(cmd);

			if (screenshotPath && world->GetTime() > screenshotDelay && width > 0)
			{
				SaveScreenshot(*device, renderer, swapchainFormat, *world.Get(), window, width, height, screenshotPath);
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
	UIRuntime::Get().Shutdown();
	SDL_ReleaseWindowFromGPUDevice(device->GetHandle(), window);
	SDL_DestroyWindow(window);
	delete device;
	JobSystem::Shutdown();
	SDL_Quit();
	Log::Flush();
	return 0;
}
