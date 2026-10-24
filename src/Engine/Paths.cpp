#include "Engine/Paths.h"

#include <SDL3/SDL_filesystem.h>

#include <filesystem>

namespace FP::Paths
{
	namespace
	{
		namespace fs = std::filesystem;

		String FindProjectDir()
		{
			fs::path starts[2];
			if (const char* base = SDL_GetBasePath())
				starts[0] = fs::path(reinterpret_cast<const char8_t*>(base));
			std::error_code ec;
			starts[1] = fs::current_path(ec);

			for (const fs::path& start : starts)
			{
				for (fs::path dir = start; !dir.empty(); dir = dir.parent_path())
				{
					if (fs::is_directory(dir / "Content", ec))
					{
						const std::u8string text = dir.generic_u8string();
						return String(reinterpret_cast<const char*>(text.data()), static_cast<int32>(text.size()));
					}
					if (dir == dir.parent_path())
						break;
				}
			}
			return String(".");
		}
	}

	const String& GetProjectDir()
	{
		static const String dir = FindProjectDir();
		return dir;
	}

	const String& GetContentDir()
	{
		static const String dir = GetProjectDir() + "/Content";
		return dir;
	}

	String Content(std::string_view relative)
	{
		return GetContentDir() + "/" + relative;
	}
}
