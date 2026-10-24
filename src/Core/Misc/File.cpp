#include "Core/Misc/File.h"

#include <cstdio>

namespace FP::File
{
	bool ReadAllText(const char* path, String& out)
	{
		FILE* file = std::fopen(path, "rb");
		if (!file)
			return false;
		std::fseek(file, 0, SEEK_END);
		const long size = std::ftell(file);
		std::fseek(file, 0, SEEK_SET);

		out.Clear();
		if (size > 0)
		{
			String buffer;
			buffer.Reserve(static_cast<int32>(size));
			char chunk[4096];
			usize read = 0;
			while ((read = std::fread(chunk, 1, sizeof(chunk), file)) > 0)
				buffer.Append(std::string_view(chunk, read));
			out = std::move(buffer);
		}
		std::fclose(file);
		return true;
	}

	bool WriteAllText(const char* path, std::string_view text)
	{
		FILE* file = std::fopen(path, "wb");
		if (!file)
			return false;
		const bool ok = std::fwrite(text.data(), 1, text.size(), file) == text.size();
		std::fclose(file);
		return ok;
	}

	bool Exists(const char* path)
	{
		FILE* file = std::fopen(path, "rb");
		if (!file)
			return false;
		std::fclose(file);
		return true;
	}
}
