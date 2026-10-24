// Standalone player. Opens a window and runs a scene with the game's systems.
// Usage: Runtime [scene relative to Content] [--screenshot out.bmp]
// --screenshot saves a frame after half a second and quits, handy as a smoke test.

#include "Engine/Engine.h"
#include "Engine/Renderer2D.h"
#include "Engine/SceneSerializer.h"
#include "Game/Game.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

using namespace FP;

int main(int argc, char** argv)
{
	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		FP_LOG_ERROR("SDL_Init failed: %s", SDL_GetError());
		return 1;
	}

	SDL_Window* window = SDL_CreateWindow("FirstPrinciples", 1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
	SDL_Renderer* sdlRenderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
	if (!sdlRenderer)
	{
		FP_LOG_ERROR("Could not create window: %s", SDL_GetError());
		return 1;
	}
	SDL_SetRenderVSync(sdlRenderer, 1);

	Engine::RegisterEngineComponents();
	Game::RegisterComponents();

	const char* scene = Game::StartScene;
	const char* screenshotPath = nullptr;
	for (int i = 1; i < argc; ++i)
	{
		if (String(argv[i]) == "--screenshot" && i + 1 < argc)
			screenshotPath = argv[++i];
		else
			scene = argv[i];
	}

	StrongObjectPtr<World> world(NewObject<World>(nullptr, "GameWorld"));
	const String scenePath = Paths::Content(scene);
	if (!SceneSerializer::LoadFromFile(*world.Get(), scenePath.CStr()))
		FP_LOG_WARN("Starting with an empty world");

	Game::AddSystems(*world.Get());
	world->BeginPlay();

	{
		Renderer2D renderer(sdlRenderer);
		uint64 lastTicks = SDL_GetTicksNS();
		int32 frame = 0;
		bool running = true;
		while (running)
		{
			SDL_Event event;
			while (SDL_PollEvent(&event))
			{
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
			Input::EndFrame();

			int width = 0;
			int height = 0;
			SDL_GetCurrentRenderOutputSize(sdlRenderer, &width, &height);
			renderer.Render(*world.Get(), Renderer2D::GetCameraView(*world.Get(), width, height));

			if (screenshotPath && world->GetTime() > 0.5)
			{
				if (SDL_Surface* shot = SDL_RenderReadPixels(sdlRenderer, nullptr))
				{
					SDL_SaveBMP(shot, screenshotPath);
					SDL_DestroySurface(shot);
					FP_LOG("Saved screenshot %s", screenshotPath);
				}
				running = false;
			}
			SDL_RenderPresent(sdlRenderer);

			// Collecting once a second keeps GC pauses rare and predictable.
			if (++frame % 60 == 0)
				GarbageCollector::Get().Collect();
		}
		world->EndPlay();
	}

	world.Reset();
	GarbageCollector::Get().Collect();
	SDL_DestroyRenderer(sdlRenderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}
