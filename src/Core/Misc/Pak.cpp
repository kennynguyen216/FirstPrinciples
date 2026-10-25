#include "Core/Misc/Pak.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <set>
#include <stdexcept>

namespace FP::Pak
{
	namespace
	{
		namespace fs = std::filesystem;
		constexpr char Magic[] = "FPPAK001";
		constexpr uint64_t HashStart = 14695981039346656037ull;

		std::string UTF8(const fs::path& path)
		{
			const auto text = path.generic_u8string();
			return { reinterpret_cast<const char*>(text.data()), text.size() };
		}

		std::string Key(std::string path)
		{
			for (char& c : path)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return path;
		}

		bool SafePath(const std::string& path)
		{
			if (path.empty() || path.size() > 4096 || path.front() == '/' || path.back() == '/')
				return false;
			size_t start = 0;
			while (start < path.size())
			{
				const size_t end = path.find('/', start);
				const std::string part = path.substr(start, end == std::string::npos ? end : end - start);
				if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ')
					return false;
				for (unsigned char c : part)
					if (c < 32 || c == '\\' || c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*')
						return false;
				const auto stem = Key(part.substr(0, part.find('.')));
				if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul"
					|| (stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) && stem[3] >= '0' && stem[3] <= '9'))
					return false;
				if (end == std::string::npos)
					break;
				start = end + 1;
			}
			return true;
		}

		void Write(std::ostream& out, uint64_t value, int bytes)
		{
			for (int i = 0; i < bytes; ++i)
				out.put(static_cast<char>((value >> (i * 8)) & 255));
		}

		uint64_t Read(std::istream& in, int bytes)
		{
			uint64_t value = 0;
			for (int i = 0; i < bytes; ++i)
			{
				const int c = in.get();
				if (c == EOF)
					throw std::runtime_error("Truncated pak header");
				value |= static_cast<uint64_t>(c) << (i * 8);
			}
			return value;
		}

		uint64_t Copy(std::istream& in, std::ostream& out, uint64_t size)
		{
			std::array<char, 65536> buffer;
			uint64_t hash = HashStart;
			while (size)
			{
				const auto bytes = static_cast<std::streamsize>(std::min<uint64_t>(size, buffer.size()));
				if (!in.read(buffer.data(), bytes))
					throw std::runtime_error("Truncated pak data or unreadable source");
				out.write(buffer.data(), bytes);
				if (!out)
					throw std::runtime_error("Could not write pak data");
				for (std::streamsize i = 0; i < bytes; ++i)
				{
					hash ^= static_cast<unsigned char>(buffer[i]);
					hash *= 1099511628211ull;
				}
				size -= bytes;
			}
			return hash;
		}
	}

	bool Cook(const std::vector<Source>& sources, const fs::path& output, std::string& error)
	{
		try
		{
			struct Entry
			{
				fs::path File;
				std::string Name;
			};
			std::vector<Entry> entries;
			std::set<std::string> names;
			for (const Source& source : sources)
			{
				if (!fs::is_directory(source.Directory) || fs::is_symlink(source.Directory))
					throw std::runtime_error("Missing or linked content directory: " + UTF8(source.Directory));
				const auto absoluteOutput = fs::weakly_canonical(output);
				const auto relativeOutput = absoluteOutput.lexically_relative(fs::canonical(source.Directory));
				if (!relativeOutput.empty() && *relativeOutput.begin() != "..")
					throw std::runtime_error("Pak output cannot be inside its source directory");
				for (const auto& file : fs::recursive_directory_iterator(source.Directory))
				{
					if (file.is_symlink())
						throw std::runtime_error("Content symlinks are not supported: " + UTF8(file.path()));
					if (file.is_directory())
						continue;
					if (!file.is_regular_file())
						throw std::runtime_error("Unsupported content file: " + UTF8(file.path()));
					const std::string name = source.MountPoint + "/" + UTF8(file.path().lexically_relative(source.Directory));
					if (!SafePath(name) || !names.insert(Key(name)).second)
						throw std::runtime_error("Invalid or duplicate cooked path: " + name);
					entries.push_back({ file.path(), name });
				}
			}
			if (entries.size() > 1000000)
				throw std::runtime_error("Too many pak entries");
			std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.Name < b.Name; });
			std::ofstream out(output, std::ios::binary | std::ios::trunc);
			if (!out)
				throw std::runtime_error("Could not create pak");
			out.write(Magic, 8);
			Write(out, entries.size(), 4);
			for (const Entry& entry : entries)
			{
				std::ifstream in(entry.File, std::ios::binary);
				if (!in)
					throw std::runtime_error("Could not open " + entry.Name);
				const uint64_t size = fs::file_size(entry.File);
				Write(out, entry.Name.size(), 4);
				Write(out, size, 8);
				const auto checksumAt = out.tellp();
				Write(out, 0, 8);
				out.write(entry.Name.data(), entry.Name.size());
				const uint64_t checksum = Copy(in, out, size);
				if (in.peek() != EOF)
					throw std::runtime_error("Content changed while cooking: " + entry.Name);
				const auto end = out.tellp();
				out.seekp(checksumAt);
				Write(out, checksum, 8);
				out.seekp(end);
			}
			out.close();
			if (!out)
				throw std::runtime_error("Could not finish pak");
			return true;
		}
		catch (const std::exception& exception)
		{
			error = exception.what();
			return false;
		}
	}

	bool Extract(const fs::path& archive, const fs::path& destination, std::string& error)
	{
		try
		{
			if (!fs::is_directory(destination) || fs::is_symlink(destination) || !fs::is_empty(destination))
				throw std::runtime_error("Pak destination must be an empty directory");
			std::ifstream in(archive, std::ios::binary);
			char magic[8];
			if (!in.read(magic, 8) || !std::equal(magic, magic + 8, Magic))
				throw std::runtime_error("Invalid pak magic or version");
			const uint64_t total = fs::file_size(archive);
			const uint64_t count = Read(in, 4);
			if (count > 1000000 || count > total / 21)
				throw std::runtime_error("Invalid pak entry count");
			std::set<std::string> names;
			for (uint64_t i = 0; i < count; ++i)
			{
				const uint64_t length = Read(in, 4), size = Read(in, 8), checksum = Read(in, 8);
				const auto position = static_cast<uint64_t>(in.tellg());
				if (length == 0 || length > 4096 || position > total || length > total - position || size > total - position - length)
					throw std::runtime_error("Invalid pak entry size");
				std::string name(length, '\0');
				if (!in.read(name.data(), length) || !SafePath(name) || !names.insert(Key(name)).second)
					throw std::runtime_error("Invalid or duplicate pak path");
				const auto path = destination / fs::path(reinterpret_cast<const char8_t*>(name.c_str()));
				fs::create_directories(path.parent_path());
				std::ofstream out(path, std::ios::binary | std::ios::trunc);
				if (!out || Copy(in, out, size) != checksum)
					throw std::runtime_error("Pak checksum failed: " + name);
				out.close();
				if (!out)
					throw std::runtime_error("Could not finish extracted asset");
			}
			if (in.peek() != EOF)
				throw std::runtime_error("Unexpected trailing pak data");
			return true;
		}
		catch (const std::exception& exception)
		{
			error = exception.what();
			return false;
		}
	}
}
