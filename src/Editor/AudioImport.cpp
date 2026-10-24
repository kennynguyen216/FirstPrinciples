#include "Editor/AudioImport.h"
#include "Engine/Audio/AudioClip.h"
#include "Engine/Paths.h"
#include <SDL3/SDL_stdinc.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <memory>

#if FP_PLATFORM_WINDOWS
#include <Windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace FP::Editor
{
	namespace
	{
		namespace fs = std::filesystem;
		String Utf8(const fs::path& path)
		{
			const auto text = path.generic_u8string();
			return String(reinterpret_cast<const char*>(text.data()), static_cast<int32>(text.size()));
		}
		fs::path Native(const String& text) { return fs::path(reinterpret_cast<const char8_t*>(text.CStr())); }
		AudioResult Error(const char* text) { return { AudioError::InvalidSource, text }; }
		bool SameCaseInsensitive(const fs::path& a, const fs::path& b)
		{
			return SDL_strcasecmp(Utf8(a).CStr(), Utf8(b).CStr()) == 0;
		}
		bool Portable(const fs::path& component)
		{
			const String name = Utf8(component);
			if (name.IsEmpty() || name == "." || name == ".." || name.EndsWith(".") || name.EndsWith(" "))
				return false;
			for (unsigned char c : name)
				if (c < 32 || std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos)
					return false;
			const int32 dot = name.Find(".");
			const String stem = (dot == INDEX_NONE ? name : name.Left(dot)).ToUpper();
			if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL")
				return false;
			if (stem.Length() == 4 && (stem.StartsWith("COM") || stem.StartsWith("LPT")) && stem[3] >= '1' && stem[3] <= '9')
				return false;
			return true;
		}
		std::FILE* Open(const fs::path& path, bool write)
		{
#if FP_PLATFORM_WINDOWS
			return _wfopen(path.c_str(), write ? L"wbx" : L"rb");
#else
			return std::fopen(path.c_str(), write ? "wbx" : "rb");
#endif
		}
	}

	AudioResult ImportAudioFile(const String& sourceText, const String& destinationText, String& relativePath, const String& contentRoot)
	{
		try
		{
			if (sourceText.View().find('\0') != std::string_view::npos || destinationText.View().find('\0') != std::string_view::npos)
				return Error("Audio paths cannot contain NUL characters.");
			const fs::path source = Native(sourceText), destination = Native(destinationText);
			if (!source.is_absolute() || !destination.is_absolute())
				return Error("Choose absolute source and destination paths.");
			const fs::path content = fs::canonical(Native(contentRoot.IsEmpty() ? Paths::GetContentDir() : contentRoot));
			const fs::path parent = fs::canonical(destination.parent_path());
			const fs::path relativeParent = parent.lexically_relative(content);
			if (relativeParent.empty() || relativeParent.is_absolute())
				return Error("Choose a destination inside Content/Audio.");
			auto component = relativeParent.begin();
			if (component == relativeParent.end() || *component != "Audio")
				return Error("Choose a destination inside Content/Audio (with that exact spelling).");
			for (const fs::path& part : relativeParent)
				if (!Portable(part))
					return Error("The destination contains an invalid or nonportable folder name.");
			if (!Portable(destination.filename()) || !Utf8(destination.extension()).EqualsIgnoreCase(".wav"))
				return Error("Choose a portable WAV filename without reserved characters or device names.");
			// Detect case-only directory names before allowing an asset into a portable project.
			fs::path checked = content;
			for (const fs::path& part : relativeParent)
			{
				for (const auto& entry : fs::directory_iterator(checked))
					if (SameCaseInsensitive(entry.path().filename(), part) && entry.path().filename() != part)
						return Error("A folder already exists with different letter casing; use its exact spelling.");
				checked /= part;
			}
			const fs::path finalPath = parent / destination.filename();
			const String relative = Utf8(relativeParent / destination.filename());
			for (const auto& entry : fs::directory_iterator(parent))
			{
				if (!SameCaseInsensitive(entry.path().filename(), destination.filename()))
					continue;
				if (entry.path().filename() != destination.filename())
					return Error("A file already exists with different letter casing; choose another filename.");
				if (fs::is_symlink(finalPath) || fs::canonical(finalPath).parent_path() != parent)
					return Error("Choose an ordinary destination file inside Content/Audio, not a link.");
				if (fs::equivalent(source, finalPath))
				{
					const auto validated = AudioClipCache::ValidateFile(Utf8(finalPath));
					if (!validated.Result)
						return validated.Result;
					relativePath = relative;
					return {};
				}
				return Error("The destination already exists. Choose another filename; audio import never overwrites files.");
			}
			if (fs::file_size(source) > 64ull * 1024 * 1024)
				return Error("The WAV exceeds the 64 MiB source-file limit.");
			const fs::path staged = parent / Native(String(".audio-import-") + Guid::New().ToString() + ".tmp");
			using File = std::unique_ptr<std::FILE, decltype(&std::fclose)>;
			File input(Open(source, false), &std::fclose);
			if (!input)
				return Error("Cannot read the source WAV. Check file access and retry.");
			struct Cleanup
			{
				fs::path Path;
				~Cleanup() { std::error_code ec; fs::remove(Path, ec); }
			} cleanup;
			File output(Open(staged, true), &std::fclose);
			if (!output)
				return Error("Cannot create a staging file. Check folder access and free disk space.");
			cleanup.Path = staged;
			std::array<char, 64 * 1024> buffer;
			uint64 total = 0;
			while (const usize count = std::fread(buffer.data(), 1, buffer.size(), input.get()))
			{
				total += count;
				if (total > 64ull * 1024 * 1024)
					return Error("The source grew beyond the 64 MiB limit during import.");
				if (std::fwrite(buffer.data(), 1, count, output.get()) != count)
					return Error("Cannot write the staged WAV. Check free disk space and retry.");
			}
			if (std::ferror(input.get()) || std::fflush(output.get()) != 0)
				return Error("The WAV copy could not be completed. Check disk access and retry.");
#if FP_PLATFORM_WINDOWS
			const int flushed = _commit(_fileno(output.get()));
#else
			const int flushed = fsync(fileno(output.get()));
#endif
			if (flushed != 0 || std::fclose(output.release()) != 0)
				return Error("Cannot flush the staged WAV. Check the destination disk and retry.");
			input.reset();
			const auto validated = AudioClipCache::ValidateFile(Utf8(staged));
			if (!validated.Result)
				return validated.Result;
#if FP_PLATFORM_WINDOWS
			const bool committed = MoveFileExW(staged.c_str(), finalPath.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
#else
			const bool committed = link(staged.c_str(), finalPath.c_str()) == 0;
#endif
			if (!committed)
				return Error("Import could not commit the WAV; the destination may now exist. Choose another filename or check disk access.");
			relativePath = relative;
			return {};
		}
		catch (const std::exception& error)
		{
			return { AudioError::InvalidSource, String::Format("Audio import failed: %s", error.what()) };
		}
	}
}
