#pragma once

#include "Core/String/String.h"

namespace FP
{
	// A game in its own folder: a <Name>.fpproject file, Content for assets and Source for
	// game code and scripts. Each project only sees its own files.
	class Project
	{
	public:
		static constexpr const char* Extension = ".fpproject";

		String Name;
		// Scene the runtime opens first, relative to Content.
		String StartScene = "Scenes/Main.scene";
		// Absolute folder holding the project file.
		String Dir;

		String GetFilePath() const { return Dir + "/" + Name + Extension; }
		String GetContentDir() const { return Dir + "/Content"; }
		String GetSourceDir() const { return Dir + "/Source"; }
		String GetScriptsDir() const { return Dir + "/Source/Scripts"; }

		bool LoadFromString(std::string_view text);
		String SaveToString() const;

		// path is a .fpproject file or a folder holding one.
		bool Load(const String& path);
		bool Save() const;

		// The .fpproject file in dir, or empty when there is none.
		static String FindFile(const String& dir);

		// Writes a new project with an empty scene and stub game code into dir/name.
		static bool Create(const String& parentDir, const String& name, Project& out, String& error);
	};
}
