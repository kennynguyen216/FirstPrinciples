#include "Engine/Paths.h"
#include "Engine/Project.h"
#include "Core/Log.h"

#include <SDL3/SDL_filesystem.h>

#include <filesystem>

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

		struct State
		{
			Project Current;
			String ContentDir;
		};

		void SetCurrent(State& state, const Project& project)
		{
			state.Current = project;
			state.ContentDir = project.GetContentDir();
		}

		State& GetState()
		{
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

	const Project& GetProject()
	{
		return GetState().Current;
	}

	const String& GetProjectDir()
	{
		return GetState().Current.Dir;
	}

	const String& GetContentDir()
	{
		return GetState().ContentDir;
	}

	String Content(std::string_view relative)
	{
		return GetContentDir() + "/" + relative;
	}

	const String& GetEngineDir()
	{
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
}
