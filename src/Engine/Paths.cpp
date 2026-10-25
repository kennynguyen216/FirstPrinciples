#include "Engine/Paths.h"
#include "Engine/Project.h"
#include "Engine/Assets/AssetDatabase.h"
#include "Core/Log.h"
#include "Core/Misc/ModuleContext.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include <filesystem>
#include <memory>
#include <atomic>
#include <mutex>

namespace FP::Paths
{
	namespace
	{
		namespace fs = std::filesystem;

		String ToString(const fs::path& path)
		{
			const std::u8string text = path.generic_u8string();
			return String(reinterpret_cast<const char*>(text.data()), static_cast<int32>(text.size()));
		}

		// The executable's folder and the working folder, where every search starts.
		Array<fs::path> SearchStarts()
		{
			Array<fs::path> starts;
			if (const char* base = SDL_GetBasePath())
				starts.Add(fs::path(reinterpret_cast<const char8_t*>(base)));
			std::error_code ec;
			starts.Add(fs::current_path(ec));
			return starts;
		}

		// First folder at or above a start for which found returns true.
		template <typename Fn>
		String WalkUp(Fn found)
		{
			for (const fs::path& start : SearchStarts())
			{
				for (fs::path dir = start; !dir.empty(); dir = dir.parent_path())
				{
					if (found(dir))
						return ToString(dir);
					if (dir == dir.parent_path())
						break;
				}
			}
			return {};
		}

		struct AssetState
		{
			std::mutex Mutex;
			std::unique_ptr<AssetDatabase> Database;
			std::atomic<AssetDatabase*> Ready{ nullptr };
		};

		struct State
		{
			Project Current;
			String ContentDir;
			String EngineDir;
			String SavedDir;
			bool Packaged = false;
			std::unique_ptr<AssetState> Assets;
		};

		void SetCurrent(State& state, const Project& project)
		{
			state.Current = project;
			state.ContentDir = project.GetContentDir();
			state.SavedDir = project.Dir + "/Saved";
			state.EngineDir.Clear();
			state.Packaged = false;
			state.Assets = std::make_unique<AssetState>();
		}

		State& GetState()
		{
			// Scripts use the host's extracted content and current project.
			if (const ModuleContext* context = GetModuleContext())
			{
				if (context->Paths)
					return *static_cast<State*>(context->Paths);
			}
			static State state = []() {
				State found;
				Project project;
				const String dir = WalkUp([](const fs::path& dir) { return !Project::FindFile(ToString(dir)).IsEmpty(); });
				bool loaded = !dir.IsEmpty() && project.Load(dir);
#ifdef FP_PROJECT_DIR
				if (!loaded)
					loaded = project.Load(FP_PROJECT_DIR);
#endif
				if (!loaded)
				{
					FP_LOG_ERROR("No project found. Pass --project with a project folder.");
					project.Name = "Untitled";
					project.Dir = ".";
				}
				SetCurrent(found, project);
				return found;
			}();
			return state;
		}
	}

	bool OpenProject(const String& path)
	{
		Project project;
		if (!project.Load(path))
			return false;
		SetCurrent(GetState(), project);
		FP_LOG("Opened project %s", project.Name.CStr());
		return true;
	}

	void UsePackagedContent(const String& directory, const String& savedDirectory)
	{
		State& state = GetState();
		state.ContentDir = directory + "/Content";
		state.EngineDir = directory + "/Engine";
		state.Current.Dir = directory;
		state.SavedDir = savedDirectory + "/Saved";
		state.Packaged = true;
		state.Assets = std::make_unique<AssetState>();
	}

	bool IsPackaged()
	{
		return GetState().Packaged;
	}

	const Project& GetProject()
	{
		return GetState().Current;
	}

	const String& GetProjectDir()
	{
		return GetState().Current.Dir;
	}

	const String& GetSavedDir()
	{
		return GetState().SavedDir;
	}

	const String& GetContentDir()
	{
		return GetState().ContentDir;
	}

	String GetSaveDir()
	{
		char* path = SDL_GetPrefPath("FirstPrinciples", GetProject().Name.CStr());
		if (!path)
			return {};
		const String dir = String(path) + "Saves";
		SDL_free(path);
		return dir;
	}

	String Content(std::string_view relative)
	{
		const String path = AssetPath(relative);
		return path.IsEmpty() ? String() : GetContentDir() + "/" + path;
	}

	AssetDatabase& GetAssets()
	{
		State& state = GetState();
		AssetState& assets = *state.Assets;
		if (AssetDatabase* ready = assets.Ready.load(std::memory_order_acquire)) return *ready;
		std::lock_guard lock(assets.Mutex);
		if (!assets.Database)
		{
			auto database = std::make_unique<AssetDatabase>();
			database->Open(state.ContentDir);
			assets.Database = std::move(database);
			// Publish only after the initial scan has finished
			assets.Ready.store(assets.Database.get(), std::memory_order_release);
		}
		return *assets.Database;
	}

	String AssetPath(std::string_view reference)
	{
		return GetAssets().Resolve(reference);
	}

	String AssetReference(std::string_view path)
	{
		return GetAssets().Reference(path);
	}

	const String& GetEngineDir()
	{
		if (GetState().Packaged)
			return GetState().EngineDir;
		static const String dir = WalkUp([](const fs::path& dir) {
			std::error_code ec;
			return fs::is_directory(dir / "Engine/Content", ec);
		}) + "/Engine";
		return dir;
	}

	String EngineContent(std::string_view relative)
	{
		return GetEngineDir() + "/Content/" + relative;
	}

	void* GetSharedState()
	{
		return &GetState();
	}
}
