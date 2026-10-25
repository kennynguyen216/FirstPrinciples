#include "Core/Misc/File.h"
#include "Core/Misc/Guid.h"

#include <cstdio>
#include <filesystem>
#include <limits>

#if FP_PLATFORM_WINDOWS
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace FP::File
{
	namespace
	{
		std::filesystem::path NativePath(const char* path)
		{
			return std::filesystem::path(reinterpret_cast<const char8_t*>(path));
		}

		FILE* Open(const char* path, bool write)
		{
			if (!path || !*path) return nullptr;
#if FP_PLATFORM_WINDOWS
			return _wfopen(NativePath(path).c_str(), write ? L"wb" : L"rb");
#else
			return std::fopen(path, write ? "wb" : "rb");
#endif
		}
	}

	bool ReadAllText(const char* path, String& out)
	{
		FILE* file = Open(path, false);
		if (!file) return false;
		String buffer;
		char chunk[4096];
		usize read;
		bool ok = true;
		while ((read = std::fread(chunk, 1, sizeof(chunk), file)) > 0)
		{
			if (read > static_cast<usize>(std::numeric_limits<int32>::max() - buffer.Length()))
			{
				ok = false;
				break;
			}
			buffer.Append(std::string_view(chunk, read));
		}
		ok = ok && std::ferror(file) == 0;
		ok = std::fclose(file) == 0 && ok;
		if (ok) out = std::move(buffer);
		return ok;
	}

	bool WriteAllText(const char* path, std::string_view text)
	{
		if (!path || !*path) return false;
		const String temporary = String(path) + ".tmp." + Guid::New().ToString();
		FILE* file = Open(temporary.CStr(), true);
		if (!file) return false;
		bool ok = std::fwrite(text.data(), 1, text.size(), file) == text.size();
		ok = std::fflush(file) == 0 && ok;
#if FP_PLATFORM_WINDOWS
		ok = _commit(_fileno(file)) == 0 && ok;
#else
		ok = fsync(fileno(file)) == 0 && ok;
#endif
		ok = std::fclose(file) == 0 && ok;
		if (ok)
		{
#if FP_PLATFORM_WINDOWS
			ok = MoveFileExW(NativePath(temporary.CStr()).c_str(), NativePath(path).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
			ok = std::rename(temporary.CStr(), path) == 0;
#endif
		}
		if (!ok)
		{
			std::error_code error;
			std::filesystem::remove(NativePath(temporary.CStr()), error);
		}
		return ok;
	}

	bool Exists(const char* path)
	{
		FILE* file = Open(path, false);
		if (!file)
			return false;
		std::fclose(file);
		return true;
	}
}
