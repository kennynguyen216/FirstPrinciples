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
#include "Engine/Physics/BlackHoleSystem.h"
#include "Engine/Physics/PlayerInputSystem.h"
#include "Engine/UI/UISystem.h"
#include "Engine/Particles/ParticleSystem.h"
#include "Engine/Audio/AudioDevice.h"
#include "Engine/Project.h"
#include "Core/Jobs/JobSystem.h"
#include "Engine/Render/Renderer.h"
#include "Engine/Render/DIA.h"
#include "Engine/Render/LinearCapture.h"
#include "Core/Misc/File.h"
#include "Engine/SceneSerializer.h"
#include "Engine/Scripting/ScriptRuntime.h"
#include "Engine/Scripting/ScriptSystem.h"
#include "Game.h"
#include "Core/Misc/Pak.h"
#include "Core/Misc/Guid.h"

#include <filesystem>
#include <cstdio>
#include "Engine/FieldSerializer.h"
#include "Engine/Material.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <filesystem>

#include <cstdlib>
#include <cmath>

using namespace FP;

namespace
{
	// Each process gets a private content directory so concurrent games do not overwrite assets.
	struct PackagedContent
	{
		std::filesystem::path Directory;

		~PackagedContent()
		{
			if (!Directory.empty())
			{
				std::error_code ec;
				std::filesystem::remove_all(Directory, ec);
			}
		}

		bool Open()
		{
			namespace fs = std::filesystem;
			const char* base = SDL_GetBasePath();
			if (!base)
				return true;
			fs::path resources = fs::path(reinterpret_cast<const char8_t*>(base));
#if FP_PLATFORM_MACOS
			if (resources.filename().empty())
				resources = resources.parent_path();
			if (resources.filename() == "MacOS" && resources.parent_path().filename() == "Contents")
				resources = resources.parent_path() / "Resources";
#endif
			std::error_code ec;
			const bool packaged = fs::exists(resources / "Package.json", ec)
				|| fs::exists(resources / "Game.fpproject", ec)
				|| fs::exists(resources / "Content.pak", ec);
			if (!packaged)
				return true;
			const auto descriptor = resources / "Game.fpproject";
			const auto descriptorText = descriptor.u8string();
			if (!Paths::OpenProject(reinterpret_cast<const char*>(descriptorText.c_str())))
				return false;
			char* pref = SDL_GetPrefPath("FirstPrinciples", Paths::GetProject().Name.CStr());
			if (!pref)
				return false;
			const fs::path saved = fs::path(reinterpret_cast<const char8_t*>(pref));
			SDL_free(pref);
			Directory = saved / "Cooked" / Guid::New().ToString().CStr();
			fs::create_directories(Directory, ec);
			std::string error;
			if (ec || !Pak::Extract(resources / "Content.pak", Directory, error))
			{
				std::fprintf(stderr, "Could not open cooked content: %s %s\n", error.c_str(), ec.message().c_str());
				return false;
			}
			const auto cookedText = Directory.u8string();
			const auto savedText = saved.u8string();
			Paths::UsePackagedContent(reinterpret_cast<const char*>(cookedText.c_str()), reinterpret_cast<const char*>(savedText.c_str()));
			fs::create_directories(saved / "Saved/Logs", ec);
			return !ec;
		}
	};

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

	bool SaveScreenshot(GPUDevice& device, Renderer& renderer, SDL_GPUTextureFormat format, World& world, const RenderView& view, uint32 width, uint32 height, const char* path)
	{
		RefPtr<GPUTexture> target = device.CreateRenderTarget(format, width, height, false);
		SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device.GetHandle());
		if (!target || !cmd) { if (cmd) SDL_CancelGPUCommandBuffer(cmd); return false; }
		if (!renderer.Render(cmd, target->GetHandle(), width, height, world, view)) { SDL_CancelGPUCommandBuffer(cmd); return false; }
		if (!SDL_SubmitGPUCommandBuffer(cmd)) return false;

		Array<uint8> pixels;
		pixels.Resize(static_cast<int32>(width * height * 4));
		if (!device.ReadPixels(target->GetHandle(), width, height, pixels.Data()))
			return false;
		const bool bgra = format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM || format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB;
		SDL_Surface* surface = SDL_CreateSurfaceFrom(static_cast<int>(width), static_cast<int>(height),
			bgra ? SDL_PIXELFORMAT_ARGB8888 : SDL_PIXELFORMAT_ABGR8888, pixels.Data(), static_cast<int>(width * 4));
		if (!surface) return false;
		const bool saved = SDL_SaveBMP(surface, path);
		SDL_DestroySurface(surface);
		FP_LOG("Saved screenshot %s", path);
		return saved;
	}

	bool CaptureState(GPUDevice& device, DIA& dia, const RenderView& view, const String& prefix)
	{
		GPUTexture* output = dia.GetOutput();
		if (!output || !dia.GetError().IsEmpty()) return false;
		const uint32 width = output->GetWidth(), height = output->GetHeight();
		const auto pixels = CaptureLinear(device, *output);
		for (const auto& pixel : pixels) if (pixel.W != 1) return false;
		if (!SaveLinearPFM((prefix + ".pfm").CStr(), pixels, width, height)) return false;
		for (uint32 i = 0; i < 6; ++i)
		{
			GPUTexture* diagnostic = dia.GetDiagnostic(i);
			if (!diagnostic || !SaveLinearRGBA(String::Format("%s.raw%u.rgba", prefix.CStr(), i).CStr(), CaptureLinear(device, *diagnostic), width, height)) return false;
		}
		GPUTexture* cache = dia.GetCacheDiagnostic();
		if (!cache || !SaveLinearRGBA((prefix + ".cache.rgba").CStr(), CaptureLinear(device, *cache), cache->GetWidth(), cache->GetHeight())) return false;
		GPUTexture* counts = dia.GetHistoryCountsDiagnostic();
		if (!counts || !SaveLinearRGBA((prefix + ".history-counts.rgba").CStr(), CaptureLinear(device, *counts), width, height)) return false;
		if (!SaveLinearRGBA((prefix + ".geometry.rgba").CStr(), dia.CaptureGeometry(view), width, height) || !SaveLinearRGBA((prefix + ".reflected.rgba").CStr(), dia.CaptureGeometry(view, true), width, height)) return false;
		const String metadata = String::Format("{\"scene_revision\":%llu,\"scene_uploads\":%u,\"immutable_mesh_builds\":%u,\"instances\":%d,\"pending_age_bound_frames\":%u,\"resident_bytes_bound\":%llu,\"cpu_update_ms\":%.9g,\"ray_backend\":\"%s\",\"acceleration_builds\":%llu}\n", static_cast<unsigned long long>(dia.GetSceneRevision()), dia.GetSceneUploadCount(), dia.GetMeshBuildCount(), dia.GetRayScene().GetInstances().Num(), dia.GetPendingAge(), static_cast<unsigned long long>(dia.GetMemoryBytes()), dia.GetUpdateMilliseconds(), dia.GetRayBackend(), static_cast<unsigned long long>(dia.GetAccelerationBuildCount()));
		return File::WriteAllText((prefix + ".json").CStr(), metadata.View());
	}

	struct SequenceEvent { uint32 Frame; Array<String> Tokens; };

	bool CaptureRasterState(GPUDevice& device, Renderer& renderer, const String& prefix)
	{
		GPUTexture* output = renderer.GetSceneColor();
		if (!output || !SaveLinearPFM((prefix + ".pfm").CStr(), CaptureLinear(device, *output), output->GetWidth(), output->GetHeight())) return false;
		const String metadata = String::Format("{\"shadow_maps\":%u}\n", renderer.GetShadowMapCount());
		return File::WriteAllText((prefix + ".json").CStr(), metadata.View());
	}
}

int main(int argc, char** argv)
{
	PackagedContent package;
	if (!package.Open())
		return 1;
	Log::OpenFile((Paths::GetSavedDir() + "/Logs/Runtime.log").CStr());

	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
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
	const char* hdrPath = nullptr;
	const char* referencePath = nullptr;
	const char* metricsPath = nullptr;
	const char* sequencePath = nullptr;
	const char* captureDir = nullptr;
	uint32 frameLimit = 0, warmup = 300, referenceSamples = 512, referenceSize = 64, seed = 911, internalHeight = 0;
	uint32 windowWidth = 1280, windowHeight = 720;
	uint32 referenceBounces = 8;
	int diaOverride = -1;
	ViewMode viewMode = ViewMode::Lit;
	auto number = [&](const char* text, uint32& result, uint32 maximum) {
		int64 value;
		if (!String(text).ToInt(value) || value < 0 || value > maximum) return false;
		result = static_cast<uint32>(value);
		return true;
	};
	for (int i = 1; i < argc; ++i)
	{
		const String option(argv[i]);
		if (option == "--voice") { voice = true; continue; }
		if (option == "--host")
		{
			host = true;
			uint32 port;
			if (i + 1 < argc && number(argv[i + 1], port, 65535) && port > 0) { hostPort = static_cast<uint16>(port); ++i; }
			continue;
		}
		if (!option.StartsWith("--")) { scene = argv[i]; continue; }
		if (i + 1 >= argc) { FP_LOG_ERROR("Missing value for %s", argv[i]); return 2; }
		const char* value = argv[++i];
		if (option == "--join") joinAddress = value;
		else if (option == "--password") password = value;
		else if (option == "--wait") { screenshotDelay = std::atof(value); if (!std::isfinite(screenshotDelay) || screenshotDelay < 0) return 2; }
		else if (option == "--screenshot") screenshotPath = value;
		else if (option == "--hdr") hdrPath = value;
		else if (option == "--reference") referencePath = value;
		else if (option == "--metrics") metricsPath = value;
		else if (option == "--sequence") sequencePath = value;
		else if (option == "--capture-dir") captureDir = value;
		else if (option == "--ray-backend" && (String(value) == "auto" || String(value) == "software" || String(value) == "hardware"))
		{
			if (!SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "FP_DIA_RAY_BACKEND", value, true)) return 2;
		}
		else if (option == "--frames" && number(value, frameLimit, 100000) && frameLimit > 0) {}
		else if (option == "--warmup" && number(value, warmup, 100000)) {}
		else if (option == "--reference-spp" && number(value, referenceSamples, 16384) && referenceSamples > 0) {}
		else if (option == "--reference-size" && number(value, referenceSize, 256) && referenceSize > 0) {}
		else if (option == "--reference-bounces" && number(value, referenceBounces, 16) && referenceBounces > 0) {}
		else if (option == "--seed" && number(value, seed, 0x7fffffffu)) {}
		else if (option == "--internal-height" && number(value, internalHeight, 1080) && internalHeight >= 32) {}
		else if (option == "--width" && number(value, windowWidth, 4096) && windowWidth >= 32) {}
		else if (option == "--height" && number(value, windowHeight, 2160) && windowHeight >= 32) {}
		else if (option == "--dia" && (String(value) == "0" || String(value) == "1")) diaOverride = value[0] - '0';
		else if (option == "--viewmode")
		{
			const String mode(value);
			if (mode == "lit") viewMode = ViewMode::Lit;
			else if (mode == "direct") viewMode = ViewMode::DIADirect;
			else if (mode == "diffuse") viewMode = ViewMode::DIADiffuse;
			else if (mode == "reflections") viewMode = ViewMode::DIAReflections;
			else if (mode == "coverage") viewMode = ViewMode::DIACoverage;
			else if (mode == "cache") viewMode = ViewMode::DIACache;
			else { FP_LOG_ERROR("Unknown view mode %s", value); return 2; }
		}
		else { FP_LOG_ERROR("Invalid option or value: %s %s", option.CStr(), value); return 2; }
	}
	if ((metricsPath || hdrPath || referencePath) && frameLimit == 0) frameLimit = warmup + 300;
	Array<SequenceEvent> events;
	if (sequencePath)
	{
		String text;
		uint32 previous = 0;
		if (!captureDir || !frameLimit || !File::ReadAllText(sequencePath, text) || !FieldSerializer::ForEachLine(text.View(), [&](int32, const Array<String>& tokens) {
			uint32 frame;
			if (tokens.Num() < 3 || tokens[0] != "at" || !number(tokens[1].CStr(), frame, frameLimit - 1) || frame < previous) return false;
			previous = frame;
			events.Add({ frame, tokens });
			return true;
		})) { FP_LOG_ERROR("Invalid sequence file"); return 2; }
		std::error_code error;
		std::filesystem::create_directories(std::filesystem::path(reinterpret_cast<const char8_t*>(captureDir)), error);
		if (error) return 2;
	}
	if (voice && !host && !joinAddress)
	{
		FP_LOG_ERROR("Voice chat needs --host or --join. Use a headset and hold V to talk.");
		return 1;
	}

	GPUDevice* device = GPUDevice::Create();
	SDL_Window* window = device ? SDL_CreateWindow(Paths::GetProject().Name.CStr(), static_cast<int>(windowWidth), static_cast<int>(windowHeight), SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY) : nullptr;
	if (!window || !SDL_ClaimWindowForGPUDevice(device->GetHandle(), window))
	{
		FP_LOG_ERROR("Could not create a GPU window: %s", SDL_GetError());
		return 1;
	}

	// Linear swapchain so the GPU applies sRGB encoding for us.
	const SDL_GPUPresentMode presentMode = metricsPath && SDL_WindowSupportsGPUPresentMode(device->GetHandle(), window, SDL_GPU_PRESENTMODE_IMMEDIATE) ? SDL_GPU_PRESENTMODE_IMMEDIATE : SDL_GPU_PRESENTMODE_VSYNC;
	if (SDL_WindowSupportsGPUSwapchainComposition(device->GetHandle(), window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR))
		SDL_SetGPUSwapchainParameters(device->GetHandle(), window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR, presentMode);
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
	// The game's controls. Projects without the file get the defaults.
	Input::LoadActionsFile(Paths::Content("Input.actions").CStr());
	const String scenePath = Paths::Content(scene);
	if (!SceneSerializer::LoadFromFile(*world.Get(), scenePath.CStr()))
	{
		if (Paths::IsPackaged() || frameLimit || hdrPath || metricsPath) return 1;
		FP_LOG_WARN("Starting with an empty world");
	}
	for (auto [entity, settings] : world->View<DIAComponent>().each())
	{
		if (diaOverride >= 0) settings.Enabled = diaOverride != 0;
		if (internalHeight) settings.InternalHeight = static_cast<int32>(internalHeight);
		settings.Seed = static_cast<int32>(seed);
	}
	if (diaOverride == 1 && world->View<DIAComponent>().begin() == world->View<DIAComponent>().end())
	{
		DIAComponent& settings = world->Add<DIAComponent>(world->CreateEntity("DIA Settings"));
		if (internalHeight) settings.InternalHeight = static_cast<int32>(internalHeight);
		settings.Seed = static_cast<int32>(seed);
	}

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

	// Development runs can use built in systems while scripts are unavailable.
	const bool scriptsLoaded = ScriptRuntime::Get().LoadModule(ScriptRuntime::GetDefaultModulePath());
	if (Paths::IsPackaged() && !scriptsLoaded)
		return 1;
	world->AddSystem<ScriptSystem>();
	world->AddSystem<PlayerInputSystem>();
	world->AddSystem<BlackHoleSystem>();
	world->AddSystem<PhysicsSystem>();
	world->AddSystem<AnimationSystem>();
	world->AddSystem<UISystem>();
	world->AddSystem<ParticleSystem>();
	Game::AddSystems(*world.Get());
	world->BeginPlay();
	int result = 0;

	{
		Renderer renderer(*device, swapchainFormat);
		renderer.PreloadTextures(*world.Get());
		uint64 lastTicks = SDL_GetTicksNS();
		int32 frame = 0;
		bool running = renderer.IsValid();
		// Typed text reaches Input so UI text boxes work.
		SDL_StartTextInput(window);
		if (!running) result = 1;
		Array<double> frameTimes;
		Array<double> updateTimes;
		Array<usize> residentBytes;
		RenderView lastView;
		uint64 historyEpoch = 0, viewIdentity = 1;
		int32 nextEvent = 0;
		const String originalScene = SceneSerializer::SaveToString(*world.Get());
		uint32 lastWidth = 0, lastHeight = 0;
		uint32 skippedPresentations = 0;
		uint32 viewportChanges = 0;
		Array<String> captures;
		uint64 runStart = SDL_GetTicksNS();
		while (running)
		{
			if (frameLimit && SDL_GetTicksNS() - runStart > 120000000000ull) { FP_LOG_ERROR("Run exceeded 120 seconds"); result = 1; break; }
			const uint64 frameStart = SDL_GetTicksNS();
			while (nextEvent < events.Num() && events[nextEvent].Frame == static_cast<uint32>(frame))
			{
				const auto& tokens = events[nextEvent++].Tokens;
				const String& action = tokens[2];
				bool valid = true;
				if (action == "capture" && tokens.Num() == 4)
				{
					for (char c : tokens[3].View()) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) valid = false;
					if (tokens[3].IsEmpty() || tokens[3].Length() > 64) valid = false;
					if (valid) captures.Add(tokens[3]);
				}
				else if (action == "cut" && tokens.Num() == 3) ++historyEpoch;
				else if (action == "view" && tokens.Num() == 4) { uint32 id; valid = number(tokens[3].CStr(), id, 65535) && id > 0; if (valid) viewIdentity = id; }
				else if (action == "resize" && tokens.Num() == 5)
				{
					uint32 w, h;
					valid = number(tokens[3].CStr(), w, 4096) && number(tokens[4].CStr(), h, 2160) && w >= 32 && h >= 32;
					if (valid) SDL_SetWindowSize(window, static_cast<int>(w), static_cast<int>(h));
				}
				else if ((action == "restore" && tokens.Num() == 3) || (action == "load" && tokens.Num() == 4))
				{
					world->EndPlay();
					valid = action == "restore" ? SceneSerializer::LoadFromString(*world.Get(), originalScene.View()) : SceneSerializer::LoadFromFile(*world.Get(), Paths::Content(tokens[3]).CStr());
					for (auto [entity, settings] : world->View<DIAComponent>().each()) { if (internalHeight) settings.InternalHeight = static_cast<int32>(internalHeight); settings.Seed = static_cast<int32>(seed); }
					world->BeginPlay();
					++historyEpoch;
				}
				else if ((action == "set" && tokens.Num() >= 7) || (action == "material" && tokens.Num() >= 6) || (action == "delete" && tokens.Num() == 4))
				{
					const Entity entity = world->FindEntityByName(tokens[3].View());
					valid = entity != NullEntity;
					if (valid && action == "delete") world->DestroyEntity(entity);
					else if (valid)
					{
						void* data = nullptr;
						const FieldInfo* field = nullptr;
						Material* material = nullptr;
						const int32 first = action == "set" ? 5 : 4;
						if (action == "set")
						{
							const ComponentInfo* info = ComponentRegistry::Get().Find(tokens[4]);
							if (info) { data = info->TryGet(world->GetRegistry(), entity); field = info->FindField(tokens[5].View()); }
						}
						else if (const auto* component = world->TryGet<MaterialComponent>(entity))
						{
							material = MaterialLibrary::Get().Load(component->Material);
							if (material) { data = &material->GetData(); for (const auto& entry : Material::GetFields()) if (tokens[4] == entry.Name) field = &entry; }
						}
						Array<String> values;
						for (int32 i = first; i < tokens.Num(); ++i) values.Add(tokens[i]);
						valid = data && field && FieldSerializer::Read(*field, data, values);
						if (valid && material) { valid = ValidateMaterial(material->GetData()); material->MarkDirty(); }
						if (valid) valid = SceneSerializer::Validate(*world.Get());
					}
				}
				else valid = false;
				if (!valid) { FP_LOG_ERROR("Invalid sequence action at frame %d", frame); result = 1; running = false; break; }
			}
			if (!running) break;
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

			SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device->GetHandle());
			if (!cmd) { FP_LOG_ERROR("GPU command acquisition failed: %s", SDL_GetError()); result = 1; break; }
			SDL_GPUTexture* swapchain = nullptr;
			uint32 width = 0;
			uint32 height = 0;
			if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window, &swapchain, &width, &height))
			{
				FP_LOG_ERROR("Swapchain acquisition failed: %s", SDL_GetError());
				SDL_CancelGPUCommandBuffer(cmd);
				result = 1;
				break;
			}
			if (!swapchain || !width || !height)
			{
				if (!frameLimit) { Input::SetDeltaTime(deltaTime); world->Update(deltaTime); audio.Update(); Input::EndFrame(); }
				if (!SDL_SubmitGPUCommandBuffer(cmd)) { FP_LOG_ERROR("GPU submission failed: %s", SDL_GetError()); result = 1; break; }
				++skippedPresentations;
				SDL_Delay(1);
				continue;
			}
			const float step = frameLimit ? 1.0f / 60.0f : deltaTime;
			Input::SetDeltaTime(step);
			world->Update(step);
			audio.Update();
			Input::EndFrame();
			{
				RenderView view = GameView(*world.Get(), window, width, height);
				view.Mode = viewMode;
				view.ViewIdentity = viewIdentity;
				view.HistoryEpoch = historyEpoch;
				if (!renderer.Render(cmd, swapchain, width, height, *world.Get(), view)) { result = 1; running = false; }
				if (lastWidth && (lastWidth != width || lastHeight != height)) ++viewportChanges;
				lastWidth = width;
				lastHeight = height;
				lastView = view;
			}
			if (!SDL_SubmitGPUCommandBuffer(cmd)) { FP_LOG_ERROR("GPU submission failed: %s", SDL_GetError()); result = 1; running = false; }
			if (static_cast<uint32>(frame) >= warmup) frameTimes.Add(static_cast<double>(SDL_GetTicksNS() - frameStart) / 1e6);
			if (static_cast<uint32>(frame) >= warmup) { updateTimes.Add(renderer.GetDIA() ? renderer.GetDIA()->GetUpdateMilliseconds() : 0); residentBytes.Add(renderer.GetDIA() ? renderer.GetDIA()->GetMemoryBytes() : 0); }
			if (renderer.GetDIA() && !renderer.GetDIA()->GetError().IsEmpty()) { result = 1; running = false; }
			for (const auto& label : captures)
			{
				const String prefix = String(captureDir) + "/" + label;
				if (!(renderer.GetDIA() ? CaptureState(*device, *renderer.GetDIA(), lastView, prefix) : CaptureRasterState(*device, renderer, prefix))) { FP_LOG_ERROR("Capture failed at frame %d", frame); result = 1; running = false; }
				if (renderer.GetDIA() && renderer.GetDIA()->GetOutput()->GetWidth() != renderer.GetSceneColor()->GetWidth())
				{
					GPUTexture* display = renderer.GetSceneColor();
					const auto pixels = CaptureLinear(*device, *display);
					bool valid = true;
					for (const auto& pixel : pixels) valid = valid && pixel.W == 1;
					if (!valid || !SaveLinearPFM((prefix + ".display.pfm").CStr(), pixels, display->GetWidth(), display->GetHeight()))
					{ FP_LOG_ERROR("Display capture failed at frame %d", frame); result = 1; running = false; }
				}
			}
			captures.Clear();
			if (frame == 0) runStart = SDL_GetTicksNS();

			if (screenshotPath && frameLimit == 0 && world->GetTime() > screenshotDelay && width > 0)
			{
				if (!SaveScreenshot(*device, renderer, swapchainFormat, *world.Get(), lastView, width, height, screenshotPath)) result = 1;
				running = false;
			}

			// Collecting once a second keeps GC pauses rare and predictable.
			if (++frame % 60 == 0)
				GarbageCollector::Get().Collect();
			if (frameLimit && static_cast<uint32>(frame) >= frameLimit) running = false;
		}
		if (frameLimit && static_cast<uint32>(frame) < frameLimit && result == 0) { FP_LOG_ERROR("Fixed-frame run ended before completing its render count"); result = 1; }
		if (result == 0 && lastWidth && lastHeight)
		{
			if (hdrPath)
			{
				GPUTexture* source = renderer.GetDIA() ? renderer.GetDIA()->GetOutput() : renderer.GetSceneColor();
				const auto pixels = CaptureLinear(*device, *source);
				if (renderer.GetDIA()) for (const auto& pixel : pixels) if (pixel.W != 1) result = 1;
				if (!SaveLinearPFM(hdrPath, pixels, source->GetWidth(), source->GetHeight())) result = 1;
			}
			if (referencePath)
			{
				if (!renderer.GetDIA()) { FP_LOG_ERROR("Reference capture needs an enabled DIA scene"); result = 1; }
				else
				{
					const auto pixels = renderer.GetDIA()->RenderReference(RenderView::FromWorld(*world.Get(), lastWidth, lastHeight), referenceSize, referenceSize, referenceSamples, seed, referenceBounces);
					if (!SaveLinearPFM(referencePath, pixels, referenceSize, referenceSize)) result = 1;
				}
			}
			if (metricsPath)
			{
				if (!SDL_WaitForGPUIdle(device->GetHandle())) result = 1;
				Array<double> gpuTrace;
				if (renderer.GetDIA()) for (const auto& sample : renderer.GetDIA()->GetTraceTimings())
					if (sample.Frame >= warmup) gpuTrace.Add(sample.Milliseconds);
				gpuTrace.Sort();
				const String gpuStats = gpuTrace.IsEmpty() ? String("null") : String::Format("{\"samples\":%d,\"p50\":%.9g,\"p95\":%.9g}",
					gpuTrace.Num(), gpuTrace[gpuTrace.Num() / 2], gpuTrace[static_cast<int32>(0.95 * (gpuTrace.Num() - 1))]);
				Array<double> sorted = frameTimes;
				sorted.Sort();
				auto percentile = [&](double q) { return sorted.IsEmpty() ? 0.0 : sorted[static_cast<int32>(q * (sorted.Num() - 1))]; };
				String metrics = String::Format("{\n  \"frames\": %d, \"measured_frames\": %d, \"warmup_frames\": %u,\n  \"cpu_submission_wait_ms\": {\"p50\": %.9g, \"p95\": %.9g, \"p99\": %.9g},\n  \"gpu_critical_ms\": null,\n  \"lighting_resident_bytes_bound\": %llu,\n  \"lighting_scene_uploads\": %u,\n  \"frame_ms\": [", frame, frameTimes.Num(), warmup, percentile(0.5), percentile(0.95), percentile(0.99), static_cast<unsigned long long>(renderer.GetDIA() ? renderer.GetDIA()->GetMemoryBytes() : 0), renderer.GetDIA() ? renderer.GetDIA()->GetSceneUploadCount() : 0);
				for (int32 i = 0; i < frameTimes.Num(); ++i) metrics.Appendf("%s%.9g", i ? "," : "", frameTimes[i]);
				metrics += "],\n  \"update_ms\": [";
				for (int32 i = 0; i < updateTimes.Num(); ++i) metrics.Appendf("%s%.9g", i ? "," : "", updateTimes[i]);
				metrics += "],\n  \"resident_bytes_bound\": [";
				for (int32 i = 0; i < residentBytes.Num(); ++i) metrics.Appendf("%s%llu", i ? "," : "", static_cast<unsigned long long>(residentBytes[i]));
				metrics.Appendf("],\n  \"shadow_maps\": %u,\n  \"lighting_frames\": %u,\n  \"history_frames\": %u,\n  \"viewport_changes\": %u,\n  \"skipped_presentations\": %u,\n  \"lighting_ray_backend\": \"%s\",\n  \"lighting_acceleration_builds\": %llu,\n  \"gpu_trace_ms\": %s\n}\n", renderer.GetShadowMapCount(), renderer.GetDIA() ? renderer.GetDIA()->GetFrameCount() : 0, renderer.GetDIA() ? renderer.GetDIA()->GetHistoryFrameCount() : 0, viewportChanges, skippedPresentations, renderer.GetDIA() ? renderer.GetDIA()->GetRayBackend() : "disabled", static_cast<unsigned long long>(renderer.GetDIA() ? renderer.GetDIA()->GetAccelerationBuildCount() : 0), gpuStats.CStr());
				if (!File::WriteAllText(metricsPath, metrics.View())) result = 1;
			}
			if (screenshotPath && frameLimit && !SaveScreenshot(*device, renderer, swapchainFormat, *world.Get(), lastView, lastWidth, lastHeight, screenshotPath)) result = 1;
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
	const bool traceShutdown = SDL_GetLogPriority(SDL_LOG_CATEGORY_GPU) <= SDL_LOG_PRIORITY_DEBUG;
	uint64 shutdownStart = SDL_GetPerformanceCounter();
	if (traceShutdown) FP_LOG("Runtime deleting GPU device");
	delete device;
	JobSystem::Shutdown();
	if (traceShutdown) FP_LOG("Runtime GPU device deleted in %.1f ms", (SDL_GetPerformanceCounter() - shutdownStart) * 1000.0 / SDL_GetPerformanceFrequency());
	shutdownStart = SDL_GetPerformanceCounter();
	if (traceShutdown) FP_LOG("Runtime shutting down SDL");
	SDL_Quit();
	if (traceShutdown) FP_LOG("Runtime SDL shutdown finished in %.1f ms, returning", (SDL_GetPerformanceCounter() - shutdownStart) * 1000.0 / SDL_GetPerformanceFrequency());
	Log::Flush();
	return result;
}
