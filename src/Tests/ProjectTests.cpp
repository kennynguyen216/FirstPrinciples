#include "Tests/TestFramework.h"
#include "Tests/TestProject.h"
#include "Engine/Assets/AssetDatabase.h"
#include "Engine/Project.h"
#include "Engine/Paths.h"
#include "Core/Misc/File.h"

#include <filesystem>
#include <array>
#include <atomic>
#include <thread>

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

FP_TEST(Project_PackagedPathsKeepSavedDataSeparate)
{
	const String original = Paths::GetProject().GetFilePath();
	const String saves = Paths::GetSaveDir();
	const String directory = Paths::GetProjectDir() + "/CookedTest";
	const String saved = Paths::GetProjectDir() + "/UserDataTest";
	Paths::UsePackagedContent(directory, saved);
	FP_EXPECT(Paths::IsPackaged());
	FP_EXPECT(Paths::GetContentDir() == directory + "/Content");
	FP_EXPECT(Paths::GetProject().GetContentDir() == Paths::GetContentDir());
	FP_EXPECT(Paths::EngineContent("Shaders/Lit.frag.msl") == directory + "/Engine/Content/Shaders/Lit.frag.msl");
	FP_EXPECT(Paths::Content("Scenes/Main.scene") == directory + "/Content/Scenes/Main.scene");
	FP_EXPECT(Paths::GetSavedDir() == saved + "/Saved");
	FP_EXPECT(Paths::GetSaveDir() == saves);
	FP_EXPECT(Paths::OpenProject(original));
	FP_EXPECT(!Paths::IsPackaged());
	FP_EXPECT(Paths::GetSavedDir() == Paths::GetProjectDir() + "/Saved");
}

FP_TEST(Project_ConcurrentAssetQueriesShareOneCompletedScan)
{
	Test::ScopedProject project;
	FP_EXPECT(File::WriteAllText((Paths::GetContentDir() + "/source.bin").CStr(), "source"));
	std::array<std::thread, 8> threads;
	std::array<AssetDatabase*, 8> databases{};
	std::array<uint64, 8> fingerprints{};
	std::atomic<bool> start{ false };
	for (usize i = 0; i < threads.size(); ++i)
		threads[i] = std::thread([&, i] {
			while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
			databases[i] = &Paths::GetAssets();
			fingerprints[i] = databases[i]->Fingerprint("source.bin");
		});
	start.store(true, std::memory_order_release);
	for (auto& thread : threads) thread.join();
	for (usize i = 0; i < threads.size(); ++i)
	{
		FP_EXPECT(databases[i] == databases[0]);
		FP_EXPECT(fingerprints[i] != 0 && fingerprints[i] == fingerprints[0]);
	}
}
