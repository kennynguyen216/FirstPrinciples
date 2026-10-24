#include "Engine/Project.h"
#include "Engine/FieldSerializer.h"
#include "Core/Log.h"
#include "Core/Misc/File.h"

#include <cctype>
#include <filesystem>

namespace FP
{
	namespace
	{
		namespace fs = std::filesystem;

		String ToString(const fs::path& path)
		{
			const std::u8string text = path.generic_u8string();
			return String(reinterpret_cast<const char*>(text.data()), static_cast<int32>(text.size()));
		}

		fs::path ToPath(const String& path)
		{
			return fs::path(reinterpret_cast<const char8_t*>(path.CStr()));
		}

		bool IsValidName(const String& name)
		{
			if (name.IsEmpty() || !std::isalpha(static_cast<unsigned char>(name[0])))
				return false;
			for (char c : name.View())
			{
				if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
					return false;
			}
			return true;
		}

		const char* EmptyScene = "# FirstPrinciples scene\nversion 2\n";

		const char* GameHeader = R"(#pragma once

#include "Engine/Engine.h"

// Entry points the Editor and Runtime call into. This is where the game plugs into the engine.
namespace Game
{
	// Register game components so the editor can show them and scenes can save them.
	void RegisterComponents();

	// Add the game's systems to a world. Called before the world starts playing.
	void AddSystems(FP::World& world);
}
)";

		const char* GameSource = R"(#include "Game.h"

using namespace FP;

namespace Game
{
	void RegisterComponents()
	{
	}

	void AddSystems(World& world)
	{
		(void)world;
	}
}
)";
	}

	bool Project::LoadFromString(std::string_view text)
	{
		bool found = false;
		FieldSerializer::ForEachLine(text, [&](int32 lineNumber, const Array<String>& tokens) {
			if (tokens.Num() < 2)
				return true;
			if (tokens[0] == "Name")
			{
				Name = tokens[1];
				found = true;
			}
			else if (tokens[0] == "StartScene")
				StartScene = tokens[1];
			else
				FP_LOG_WARN("Project line %d: unknown setting %s", lineNumber, tokens[0].CStr());
			return true;
		});
		return found && !Name.IsEmpty();
	}

	String Project::SaveToString() const
	{
		String out = "# FirstPrinciples project\n";
		out += "Name ";
		FieldSerializer::AppendQuoted(out, Name);
		out += "\nStartScene ";
		FieldSerializer::AppendQuoted(out, StartScene);
		out += "\n";
		return out;
	}

	String Project::FindFile(const String& dir)
	{
		std::error_code ec;
		for (const fs::directory_entry& entry : fs::directory_iterator(ToPath(dir), ec))
		{
			if (entry.is_regular_file(ec) && entry.path().extension() == Extension)
				return ToString(entry.path());
		}
		return {};
	}

	bool Project::Load(const String& path)
	{
		std::error_code ec;
		String file = path;
		if (fs::is_directory(ToPath(path), ec))
			file = FindFile(path);
		String text;
		if (file.IsEmpty() || !File::ReadAllText(file.CStr(), text) || !LoadFromString(text.View()))
		{
			FP_LOG_ERROR("No project at %s", path.CStr());
			return false;
		}
		Dir = ToString(fs::absolute(ToPath(file), ec).lexically_normal().parent_path());
		return true;
	}

	bool Project::Save() const
	{
		return File::WriteAllText(GetFilePath().CStr(), SaveToString().View());
	}

	bool Project::Create(const String& parentDir, const String& name, Project& out, String& error)
	{
		if (!IsValidName(name))
		{
			error = "Use letters, digits and underscores, starting with a letter.";
			return false;
		}
		std::error_code ec;
		const fs::path dir = fs::absolute(ToPath(parentDir) / ToPath(name), ec).lexically_normal();
		if (fs::exists(dir, ec))
		{
			error = String::Format("%s already exists.", ToString(dir).CStr());
			return false;
		}

		Project project;
		project.Name = name;
		project.Dir = ToString(dir);
		for (const String& folder : { project.GetContentDir() + "/Scenes", project.GetScriptsDir() })
			fs::create_directories(ToPath(folder), ec);

		const bool written = !ec
			&& project.Save()
			&& File::WriteAllText((project.GetContentDir() + "/" + project.StartScene).CStr(), EmptyScene)
			&& File::WriteAllText((project.GetSourceDir() + "/Game.h").CStr(), GameHeader)
			&& File::WriteAllText((project.GetSourceDir() + "/Game.cpp").CStr(), GameSource);
		if (!written)
		{
			error = String::Format("Could not write %s", project.Dir.CStr());
			return false;
		}
		out = project;
		return true;
	}
}
