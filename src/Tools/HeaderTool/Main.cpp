// HeaderTool: reads the script sources and writes the reflection header they all include.
// Usage: HeaderTool <scripts folder> <output header>
// Runs before every GameScripts build. The output is only rewritten when it changes, so
// unchanged scripts are not recompiled.

#include "Tools/HeaderTool/HeaderParser.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;
using namespace FP::HeaderTool;

namespace
{
	bool ReadText(const fs::path& path, std::string& out)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file)
			return false;
		std::ostringstream text;
		text << file.rdbuf();
		out = text.str();
		return true;
	}
}

int main(int argc, char** argv)
{
	if (argc < 3)
	{
		std::fprintf(stderr, "Usage: HeaderTool <scripts folder> <output header>\n");
		return 2;
	}
	const fs::path source = argv[1];
	const fs::path output = argv[2];

	// Sorted so the output never depends on directory order.
	std::vector<fs::path> files;
	std::error_code ec;
	if (fs::is_directory(source, ec))
	{
		for (const fs::directory_entry& entry : fs::recursive_directory_iterator(source, ec))
		{
			const std::string extension = entry.path().extension().string();
			if (entry.is_regular_file() && (extension == ".h" || extension == ".hpp" || extension == ".cpp"))
				files.push_back(entry.path());
		}
	}
	std::sort(files.begin(), files.end());

	std::vector<Class> classes;
	std::vector<Error> errors;
	for (const fs::path& file : files)
	{
		std::string text;
		if (!ReadText(file, text))
		{
			errors.push_back({ file.string(), 0, "could not read the file" });
			continue;
		}
		ParseFile(text, fs::absolute(file).lexically_normal().string(), classes, errors);
	}

	// Two classes with one name would clash in the editor and in scene files.
	std::map<std::string, const Class*> seen;
	for (const Class& type : classes)
	{
		if (seen.count(type.Name))
			errors.push_back({ type.File, type.Line, "a script named " + type.Name + " already exists in " + seen[type.Name]->File });
		else
			seen[type.Name] = &type;
	}

	// Same format as compiler errors so the editor can jump to them.
	for (const Error& error : errors)
		std::fprintf(stderr, "%s:%d: error: %s\n", error.File.c_str(), error.Line, error.Message.c_str());
	if (!errors.empty())
		return 1;

	const std::string text = Generate(classes, source.filename().string());
	std::string existing;
	if (!ReadText(output, existing) || existing != text)
	{
		fs::create_directories(output.parent_path(), ec);
		std::ofstream file(output, std::ios::binary | std::ios::trunc);
		if (!file)
		{
			std::fprintf(stderr, "%s: error: could not write the reflection header\n", output.string().c_str());
			return 1;
		}
		file << text;
		std::printf("HeaderTool: reflected %d script classes\n", static_cast<int>(classes.size()));
	}
	return 0;
}
