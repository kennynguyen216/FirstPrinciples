#pragma once

#include "Core/String/String.h"

namespace FP
{
	class Project;
	class AssetDatabase;
}

namespace FP::Paths
{
	// Opens the project at path, a .fpproject file or its folder. Call at startup, before
	// anything loads content.
	bool OpenProject(const String& path);

	// Redirects content to an extracted package and keeps saved data outside the app.
	void UsePackagedContent(const String& directory, const String& savedDirectory);
	bool IsPackaged();

	// The open project. Without OpenProject it is found by walking up from the executable or
	// the working folder to a .fpproject file, then falls back to the one the build was made for.
	const Project& GetProject();

	const String& GetProjectDir();

	// Writable saved data for the project, including packaged games.
	const String& GetSavedDir();

	// The open project's Content folder.
	const String& GetContentDir();

	AssetDatabase& GetAssets();
	String AssetPath(std::string_view reference);
	// Use an ID when an authored reference must survive moves
	String AssetReference(std::string_view path);

	// Writable per user folder for this project's saves. Empty if it cannot be opened.
	String GetSaveDir();

	// Absolute path for a path relative to Content.
	String Content(std::string_view relative);

	// The Engine folder with content every project shares, found by walking up the same way.
	const String& GetEngineDir();

	// Absolute path for a path relative to Engine/Content, like Shaders/Lit.frag.msl.
	String EngineContent(std::string_view relative);

	// For ModuleContext so scripts use the project opened by the host.
	void* GetSharedState();
}
