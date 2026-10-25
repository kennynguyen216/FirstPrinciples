#include "Tests/TestFramework.h"
#include "Engine/SaveData.h"
#include "Engine/Paths.h"
#include "Engine/Project.h"
#include "Engine/Scripting/ScriptRuntime.h"
#include "Engine/Scripting/ScriptSystem.h"
#include "Core/Misc/File.h"

#include <filesystem>
#include <limits>

using namespace FP;

namespace
{
	namespace fs = std::filesystem;

	struct SaveFolder
	{
		fs::path Path = fs::temp_directory_path() / ("FPSaveTest-" + std::string(Guid::New().ToString().View()));
		~SaveFolder() { std::error_code ec; fs::remove_all(Path, ec); }
		String File(const char* name) const
		{
			const auto text = (Path / reinterpret_cast<const char8_t*>(name)).generic_u8string();
			return String(reinterpret_cast<const char*>(text.data()), static_cast<int32>(text.size()));
		}
	};
}

FP_TEST(Save_RoundTripsEveryType)
{
	SaveData data;
	data.SetBool("finished", true);
	data.SetInt("score", std::numeric_limits<int32>::min());
	data.SetInt("best", std::numeric_limits<int32>::max());
	data.SetFloat("volume", 0.123456789f);
	data.SetFloat("small", std::numeric_limits<float>::denorm_min());
	data.SetVec2("cursor", { 0.5f, -2 });
	data.SetVec3("position", { 1, 2, -3 });
	data.SetColor("tint", { 0.1f, 0.2f, 0.3f, 0.4f });
	data.SetString("player\n\"name", "雪 \"quoted\"\\path\n\t\r");
	data.SetString("empty", "");
	data.SetString("binary", std::string_view("a\0b", 3));

	SaveData loaded;
	loaded.SetInt("old", 1);
	FP_EXPECT(loaded.LoadFromString(data.SaveToString().View()));
	FP_EXPECT(loaded.SaveToString() == data.SaveToString());
	FP_EXPECT(loaded.Num() == 11);
	FP_EXPECT(!loaded.Has("old"));
	FP_EXPECT(loaded.GetBool("finished"));
	FP_EXPECT(loaded.GetInt("score") == std::numeric_limits<int32>::min());
	FP_EXPECT(loaded.GetInt("best") == std::numeric_limits<int32>::max());
	FP_EXPECT(loaded.GetFloat("volume") == 0.123456789f);
	FP_EXPECT(loaded.GetFloat("small") == std::numeric_limits<float>::denorm_min());
	FP_EXPECT(loaded.GetVec2("cursor") == Vec2(0.5f, -2));
	FP_EXPECT(loaded.GetVec3("position") == Vec3(1, 2, -3));
	FP_EXPECT(loaded.GetColor("tint") == (Color{ 0.1f, 0.2f, 0.3f, 0.4f }));
	FP_EXPECT(loaded.GetString("player\n\"name") == "雪 \"quoted\"\\path\n\t\r");
	FP_EXPECT(loaded.GetString("empty", "fallback").IsEmpty());
	FP_EXPECT(loaded.GetString("binary").View() == std::string_view("a\0b", 3));
}

FP_TEST(Save_DefaultsAndEdits)
{
	SaveData data;
	data.SetInt("value", 9);
	FP_EXPECT(data.GetInt("missing", 12) == 12);
	FP_EXPECT(data.GetBool("missing", true));
	FP_EXPECT(data.GetFloat("value", 0.5f) == 0.5f);
	FP_EXPECT(data.GetString("value", "default") == "default");
	data.SetString("value", "changed type");
	FP_EXPECT(data.GetString("value") == "changed type");
	FP_EXPECT(data.GetInt("value", 4) == 4);
	FP_EXPECT(data.Num() == 1);
	FP_EXPECT(data.Remove("value"));
	FP_EXPECT(!data.Remove("value"));
	FP_EXPECT(!data.Has("value"));
	data.SetBool("other", true);
	data.Clear();
	FP_EXPECT(data.Num() == 0);
	FP_EXPECT(data.LoadFromString(data.SaveToString().View()));
}

FP_TEST(Save_RejectsDamagedDataWithoutChangingValues)
{
	SaveData data;
	data.SetInt("score", 42);
	const String before = data.SaveToString();
	for (const char* text : {
		"", "FPSave 2\nend\n", "FPSave 1\nInt score 7\n",
		"FPSave 1\nInt score 7\nInt score 8\nend\n",
		"FPSave 1\nInt score 2147483648\nend\n",
		"FPSave 1\nFloat volume nan\nend\n",
		"FPSave 1\nFloat volume 1e100\nend\n",
		"FPSave 1\nBool mute yes\nend\n",
		"FPSave 1\nVec3 position 1 2\nend\n",
		"FPSave 1\nUnknown key 1\nend\n",
		"FPSave 1\nString player \"broken\nend\n",
		"FPSave 1\nend\nInt score 7\n" })
	{
		FP_EXPECT(!data.LoadFromString(text));
		FP_EXPECT(data.SaveToString() == before);
	}
	FP_EXPECT(!data.LoadFromString(std::string(16 * 1024 * 1024 + 1, ' ')));
}

FP_TEST(Save_FileReplacementAndFailures)
{
	SaveFolder folder;
	const String path = folder.File("nested/進行.save");
	SaveData data;
	data.SetInt("score", 10);
	FP_EXPECT(data.SaveToFile(path));
	SaveData loaded;
	FP_EXPECT(loaded.LoadFromFile(path));
	FP_EXPECT(loaded.GetInt("score") == 10);
	data.SetInt("score", 20);
	FP_EXPECT(data.SaveToFile(path));
	FP_EXPECT(loaded.LoadFromFile(path));
	FP_EXPECT(loaded.GetInt("score") == 20);

	data.SetFloat("invalid", std::numeric_limits<float>::infinity());
	FP_EXPECT(data.SaveToString().IsEmpty());
	FP_EXPECT(!data.SaveToFile(path));
	FP_EXPECT(loaded.LoadFromFile(path));
	FP_EXPECT(loaded.GetInt("score") == 20);
	FP_EXPECT(!loaded.Has("invalid"));
	data.Remove("invalid");
	FP_EXPECT(!data.SaveToFile(path + "/child.save"));
	FP_EXPECT(!data.SaveToFile(folder.File("nested")));
	FP_EXPECT(!data.SaveToFile(""));
	FP_EXPECT(!data.SaveToFile(String(std::string_view("bad\0path", 8))));
	const String before = loaded.SaveToString();
	FP_EXPECT(!loaded.LoadFromFile(folder.File("missing.save")));
	FP_EXPECT(!loaded.LoadFromFile(folder.File("nested")));
	FP_EXPECT(File::WriteAllText(folder.File("broken.save").CStr(), "FPSave 1\nInt score 99\n"));
	FP_EXPECT(!loaded.LoadFromFile(folder.File("broken.save")));
	FP_EXPECT(loaded.SaveToString() == before);
	for (const auto& entry : fs::directory_iterator(folder.Path))
		FP_EXPECT(!entry.path().filename().string().starts_with(".save-"));
	for (const auto& entry : fs::directory_iterator(folder.Path / "nested"))
		FP_EXPECT(!entry.path().filename().string().starts_with(".save-"));
}

FP_TEST(Save_SlotsAndSettingsAreIndependent)
{
	const String slot = String("test-") + Guid::New().ToString();
	const String settings = slot + "-settings";
	SaveData progress;
	progress.SetInt("score", 12);
	SaveData preferences;
	preferences.SetFloat("volume", 0.25f);
	FP_EXPECT(!SaveData::Exists(slot.View()));
	FP_EXPECT(progress.Save(slot.View()));
	FP_EXPECT(preferences.Save(settings.View()));
	FP_EXPECT(SaveData::Exists(slot.View()));
	SaveData loaded;
	FP_EXPECT(loaded.Load(slot.View()));
	FP_EXPECT(loaded.GetInt("score") == 12);
	FP_EXPECT(loaded.Load(settings.View()));
	FP_EXPECT(loaded.GetFloat("volume") == 0.25f);
	FP_EXPECT(!loaded.Has("score"));
	FP_EXPECT(SaveData::Delete(slot.View()));
	FP_EXPECT(!SaveData::Exists(slot.View()));
	FP_EXPECT(!SaveData::Delete(slot.View()));
	FP_EXPECT(SaveData::Exists(settings.View()));
	FP_EXPECT(SaveData::Delete(settings.View()));
}

FP_TEST(Save_RejectsInvalidSlots)
{
	SaveData data;
	data.SetInt("score", 12);
	const String before = data.SaveToString();
	for (const char* slot : { "", "../escape", "/absolute", "folder/slot", "folder\\slot", "slot.save", "Upper", "a:b", "a b" })
	{
		FP_EXPECT(!data.Save(slot));
		FP_EXPECT(!data.Load(slot));
		FP_EXPECT(!SaveData::Exists(slot));
		FP_EXPECT(!SaveData::Delete(slot));
	}
	FP_EXPECT(!data.Save(std::string(65, 'a')));
	FP_EXPECT(!data.Save(std::string_view("slot\0other", 10)));
	FP_EXPECT(data.SaveToString() == before);
}

FP_TEST(Save_ScriptsUseTheHostProjectAfterReload)
{
	SaveFolder folder;
	const String previous = Paths::GetProject().GetFilePath();
	const String name = String("SaveTest_") + Guid::New().ToString();
	const String projectFile = folder.File("Test.fpproject");
	Project project;
	project.Name = name;
	FP_EXPECT(fs::create_directories(folder.Path));
	FP_EXPECT(File::WriteAllText(projectFile.CStr(), project.SaveToString().View()));
	FP_EXPECT(Paths::OpenProject(projectFile));
	const String saveDir = Paths::GetSaveDir();

	Engine::RegisterEngineComponents();
	ScriptRuntime& runtime = ScriptRuntime::Get();
	FP_EXPECT(runtime.LoadModule(ScriptRuntime::GetDefaultModulePath()));
	const ScriptType* type = runtime.FindType("Checkpoint");
	FP_EXPECT(type != nullptr);
	if (type)
	{
		StrongObjectPtr<World> world(NewObject<World>());
		const Entity entity = world->CreateEntity("Player");
		world->SetWorldPosition(entity, { 1, 2, 3 });
		world->Add<ScriptComponent>(entity).Scripts.Emplace("Checkpoint", "Slot \"test-module\"\nScore 25\n");
		world->AddSystem<ScriptSystem>();
		world->BeginPlay();
		Script* instance = world->Get<ScriptComponent>(entity).Scripts[0].Instance;
		FP_EXPECT(instance != nullptr);
		if (instance)
		{
			for (const ScriptFunction& function : type->Functions)
				if (std::string_view(function.Name) == "SaveProgress") function.Call(instance);
			SaveData loaded;
			FP_EXPECT(loaded.Load("test-module"));
			FP_EXPECT(loaded.GetInt("score") == 25);
			FP_EXPECT(loaded.GetVec3("position") == Vec3(1, 2, 3));
			FP_EXPECT(runtime.Reload());
			world->SetWorldPosition(entity, { 4, 5, 6 });
			instance = world->Get<ScriptComponent>(entity).Scripts[0].Instance;
			type = runtime.FindType("Checkpoint");
			if (type && instance)
			{
				for (const ScriptFunction& function : type->Functions)
					if (std::string_view(function.Name) == "LoadProgress") function.Call(instance);
			}
			FP_EXPECT(world->GetWorldPosition(entity) == Vec3(1, 2, 3));
		}
		world->EndPlay();
	}
	runtime.UnloadModule();
	FP_EXPECT(SaveData::Delete("test-module"));
	FP_EXPECT(Paths::OpenProject(previous));
	std::error_code ec;
	fs::remove_all(fs::path(reinterpret_cast<const char8_t*>(saveDir.CStr())).parent_path(), ec);
}
