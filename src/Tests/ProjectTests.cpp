#include "Tests/TestFramework.h"
#include "Engine/Project.h"
#include "Core/Misc/File.h"

#include <filesystem>

using namespace FP;

FP_TEST(Project_RoundTrip)
{
	Project project;
	project.Name = "Space";
	project.StartScene = "Scenes/Level 1.scene";

	Project loaded;
	FP_EXPECT(loaded.LoadFromString(project.SaveToString().View()));
	FP_EXPECT(loaded.Name == "Space");
	FP_EXPECT(loaded.StartScene == "Scenes/Level 1.scene");
}

FP_TEST(Project_NeedsAName)
{
	Project project;
	FP_EXPECT(!project.LoadFromString("StartScene Scenes/Main.scene\n"));
}

FP_TEST(Project_CreateAndLoad)
{
	namespace fs = std::filesystem;
	std::error_code ec;
	const fs::path root = fs::temp_directory_path(ec) / "FPProjectTest";
	fs::remove_all(root, ec);
	const String parent = root.generic_string().c_str();

	Project created;
	String error;
	FP_EXPECT(!Project::Create(parent, "1Bad", created, error));
	FP_EXPECT(Project::Create(parent, "Space", created, error));
	FP_EXPECT(File::Exists((created.GetContentDir() + "/Scenes/Main.scene").CStr()));
	FP_EXPECT(File::Exists((created.GetSourceDir() + "/Game.cpp").CStr()));
	FP_EXPECT(fs::is_directory(created.GetScriptsDir().CStr()));
	FP_EXPECT(!Project::Create(parent, "Space", created, error));

	// Loads from the folder or the file itself.
	Project fromDir;
	FP_EXPECT(fromDir.Load(parent + "/Space"));
	FP_EXPECT(fromDir.Name == "Space");
	FP_EXPECT(fromDir.Dir == created.Dir);
	Project fromFile;
	FP_EXPECT(fromFile.Load(created.GetFilePath()));
	FP_EXPECT(fromFile.Dir == created.Dir);

	fs::remove_all(root, ec);
}
