#include "Tests/TestFramework.h"
#include "Engine/Assets/AssetDatabase.h"
#include "Engine/Engine.h"
#include "Engine/Paths.h"
#include "Engine/Project.h"
#include "Engine/SceneSerializer.h"
#include "Engine/Material.h"
#include "Engine/Render/TextureCache.h"
#include "Engine/Render/MeshLibrary.h"
#include "Engine/UI/UISystem.h"
#include "Engine/Scripting/Script.h"
#include <SDL3/SDL.h>
#include "Core/Misc/File.h"

#include <filesystem>
#include <fstream>
#include <chrono>
#include <thread>

using namespace FP;

namespace
{
	class AssetFieldScript : public Script
	{
	public:
		String Image;
	};
}

FP_SCRIPT_FIELDS(AssetFieldScript)
{
	fields.Field("Image", &AssetFieldScript::Image).AssetPath("Images (*.png)");
}

namespace
{
	namespace fs = std::filesystem;

	struct AssetsFixture
	{
		fs::path Root = fs::temp_directory_path() / ("FPAssets-" + std::string(Guid::New().ToString().View()));
		AssetDatabase Database;

		AssetsFixture()
		{
			Engine::RegisterEngineComponents();
			fs::create_directories(Root);
		}

		~AssetsFixture()
		{
			std::error_code ec;
			fs::remove_all(Root, ec);
		}

		String Path() const { return String(Root.generic_string()); }
		void Write(const char* path, std::string_view bytes)
		{
			fs::create_directories((Root / path).parent_path());
			std::ofstream file(Root / path, std::ios::binary);
			file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		}
		String Read(const char* path)
		{
			std::ifstream file(Root / path, std::ios::binary);
			return String(std::string(std::istreambuf_iterator<char>(file), {}));
		}
	};

	struct ProjectFixture
	{
		String Original = Paths::GetProject().GetFilePath();
		AssetsFixture Files;
		ProjectFixture()
		{
			Files.Write("Test.fpproject", "Name Test\nStartScene Scenes/Main.scene\n");
			Files.Write("Content/Textures/Brick.png", "image");
			FP_EXPECT(Paths::OpenProject(Files.Path()));
			FP_EXPECT(Paths::GetAssets().Open(Paths::GetContentDir(), true));
		}
		~ProjectFixture()
		{
			MaterialLibrary::Get().Clear();
			FP_EXPECT(Paths::OpenProject(Original));
		}
	};
}

FP_TEST(Assets_ImportPreservesIdentityAndSettings)
{
	AssetsFixture fixture;
	fixture.Write("Textures/Brick.png", "one");
	FP_EXPECT(fixture.Database.Open(fixture.Path(), true));
	const String reference = fixture.Database.Reference("Textures/Brick.png");
	FP_EXPECT(reference.StartsWith("asset://"));
	const uint64 before = fixture.Database.Fingerprint(reference);
	FP_EXPECT(fixture.Database.SetImportSettings(reference, { false, AssetColorSpace::Linear }));
	FP_EXPECT(fixture.Database.Fingerprint(reference) != before);
	fixture.Write("Textures/Brick.png", "two");
	FP_EXPECT(fixture.Database.Refresh());
	FP_EXPECT(fixture.Database.Reference("Textures/Brick.png") == reference);
	AssetDatabase reopened;
	FP_EXPECT(reopened.Open(fixture.Path()));
	FP_EXPECT(reopened.Reference("Textures/Brick.png") == reference);
	const auto* asset = reopened.Find(reference);
	FP_EXPECT(asset && !asset->Settings.GenerateMipmaps && asset->Settings.ColorSpace == AssetColorSpace::Linear);
}

FP_TEST(Assets_RuntimeScanDoesNotWrite)
{
	AssetsFixture fixture;
	fixture.Write("texture.png", "image");
	FP_EXPECT(fixture.Database.Open(fixture.Path()));
	FP_EXPECT(!fs::exists(fixture.Root / "texture.png.fpmeta"));
	FP_EXPECT(fixture.Database.Resolve("texture.png") == "texture.png");
	FP_EXPECT(!fixture.Database.SetImportSettings("texture.png", {}));
}

FP_TEST(Assets_MoveMigratesLegacyReferencesAndKeepsAliases)
{
	AssetsFixture fixture;
	fixture.Write("old.png", "image");
	fixture.Write("main.scene", "version 2\nentity 11111111-1111-4111-8111-111111111111\ncomponent Sprite\n  Texture old.png\n");
	fixture.Write("main.material", "BaseColorMap \"old.png\"\n");
	FP_EXPECT(fixture.Database.Open(fixture.Path(), true));
	const String reference = fixture.Database.Reference("old.png");
	String error;
	FP_EXPECT(fixture.Database.Move("old.png", "renamed.png", error));
	FP_EXPECT(fixture.Database.Resolve(reference) == "renamed.png");
	FP_EXPECT(fixture.Database.Resolve("old.png") == "renamed.png");
	FP_EXPECT(fixture.Read("main.scene").Contains(reference));
	FP_EXPECT(fixture.Read("main.material").Contains(reference));
	FP_EXPECT(!fs::exists(fixture.Root / "old.png.fpmeta"));
	FP_EXPECT(fs::exists(fixture.Root / "renamed.png.fpmeta"));
	AssetDatabase reopened;
	FP_EXPECT(reopened.Open(fixture.Path()));
	FP_EXPECT(reopened.Resolve(reference) == "renamed.png");
	FP_EXPECT(reopened.Dependents(reference).Num() == 2);
}

FP_TEST(Assets_FolderMovePreservesInternalRelativeReferences)
{
	AssetsFixture fixture;
	fixture.Write("Models/mesh.gltf", "{\"buffers\":[{\"uri\":\"mesh.bin\"}]}");
	fixture.Write("Models/mesh.bin", "buffer");
	FP_EXPECT(fixture.Database.Open(fixture.Path(), true));
	const String reference = fixture.Database.Reference("Models/mesh.gltf");
	String error;
	FP_EXPECT(fixture.Database.Move("Models", "Moved", error));
	FP_EXPECT(fixture.Database.Resolve(reference) == "Moved/mesh.gltf");
	FP_EXPECT(fixture.Database.GetIssues().IsEmpty());
	FP_EXPECT(!fixture.Database.Move("Moved/mesh.bin", "Moved/other.bin", error));
	FP_EXPECT(fs::exists(fixture.Root / "Moved/mesh.bin"));
}

FP_TEST(Assets_DependenciesInvalidateTransitively)
{
	AssetsFixture fixture;
	fixture.Write("Textures/Brick.png", "old");
	fixture.Write("Materials/Brick.material", "BaseColorMap \"Textures/Brick.png\"\n");
	fixture.Write("main.scene", "component Material\n  Material \"Materials/Brick.material\"\n");
	FP_EXPECT(fixture.Database.Open(fixture.Path(), true));
	const uint64 before = fixture.Database.Fingerprint("main.scene");
	fixture.Write("Textures/Brick.png", "new");
	FP_EXPECT(fixture.Database.Refresh());
	FP_EXPECT(fixture.Database.Fingerprint("main.scene") != before);
	FP_EXPECT(fixture.Database.Dependents("Textures/Brick.png").Contains("Materials/Brick.material"));
	fs::remove(fixture.Root / "Textures/Brick.png");
	FP_EXPECT(!fixture.Database.Refresh());
	FP_EXPECT(fixture.Database.GetIssues().Num() == 1);
}

FP_TEST(Assets_TracksModelAndWebDependencies)
{
	AssetsFixture fixture;
	fixture.Write("Models/mesh.gltf", "{\"buffers\":[{\"uri\":\"mesh.bin\"}],\"images\":[{\"uri\":\"../im%61ge.png\"},{\"uri\":\"data:image/png;base64,AAAA\"}]}");
	fixture.Write("Models/mesh.bin", "buffer");
	fixture.Write("image.png", "image");
	fixture.Write("UI/main.html", "<link href=\"style.css?v=2\"><script src=\"script.js\"></script><img src=\"/image.png\"><a href=\"https://example.com\">link</a>");
	fixture.Write("UI/style.css", "body { background: url('../image.png') }");
	fixture.Write("UI/script.js", "import './helper.js'");
	fixture.Write("UI/helper.js", "");
	FP_EXPECT(fixture.Database.Open(fixture.Path(), true));
	const auto* model = fixture.Database.Find("Models/mesh.gltf");
	FP_EXPECT(model && model->Dependencies.Num() == 2);
	const auto* page = fixture.Database.Find("UI/main.html");
	FP_EXPECT(page && page->Dependencies.Num() == 3);
	const uint64 before = fixture.Database.Fingerprint("UI/main.html");
	fixture.Write("UI/helper.js", "changed");
	FP_EXPECT(fixture.Database.Refresh());
	FP_EXPECT(fixture.Database.Fingerprint("UI/main.html") != before);
}

FP_TEST(Assets_RejectsDuplicateAndDamagedMetadata)
{
	AssetsFixture fixture;
	fixture.Write("one.png", "one");
	fixture.Write("two.png", "two");
	FP_EXPECT(fixture.Database.Open(fixture.Path(), true));
	const String reference = fixture.Database.Reference("one.png");
	fixture.Write("two.png.fpmeta", fixture.Read("one.png.fpmeta"));
	FP_EXPECT(!fixture.Database.Refresh());
	FP_EXPECT(fixture.Database.Find(reference) == nullptr);
	fixture.Write("one.png.fpmeta", "version 9\nid broken\n");
	const String damaged = fixture.Read("one.png.fpmeta");
	FP_EXPECT(!fixture.Database.Refresh());
	FP_EXPECT(fixture.Read("one.png.fpmeta") == damaged);
}

FP_TEST(Assets_MissingIdsAndUnsafePathsFailClosed)
{
	AssetsFixture fixture;
	fixture.Write("one.png", "one");
	FP_EXPECT(fixture.Database.Open(fixture.Path(), true));
	FP_EXPECT(fixture.Database.Resolve("asset://11111111-1111-4111-8111-111111111111").IsEmpty());
	FP_EXPECT(fixture.Database.Resolve("asset://broken").IsEmpty());
	FP_EXPECT(fixture.Database.Resolve("../outside.png").IsEmpty());
	String error;
	FP_EXPECT(!fixture.Database.Move("one.png", "../outside.png", error));
	FP_EXPECT(!fixture.Database.Move("one.png", "one.png", error));
	FP_EXPECT(fs::exists(fixture.Root / "one.png"));
}

FP_TEST(Assets_SceneRoundTripSurvivesMoveWithUnsavedEdits)
{
	ProjectFixture fixture;
	World world;
	const auto entity = world.CreateEntity("Unsaved");
	world.Add<SpriteComponent>(entity).Texture = "Textures/Brick.png";
	world.Get<TransformComponent>(entity).Position.X = 42;
	String error;
	FP_EXPECT(Paths::GetAssets().Move("Textures/Brick.png", "Textures/Renamed.png", error));
	const String text = SceneSerializer::SaveToString(world);
	FP_EXPECT(text.Contains("asset://"));
	World loaded;
	FP_EXPECT(SceneSerializer::LoadFromString(loaded, text));
	const auto copy = loaded.FindEntityByName("Unsaved");
	FP_EXPECT(loaded.IsValid(copy));
	if (!loaded.IsValid(copy)) return;
	FP_EXPECT(loaded.Get<TransformComponent>(copy).Position.X == 42);
	FP_EXPECT(Paths::AssetPath(loaded.Get<SpriteComponent>(copy).Texture) == "Textures/Renamed.png");
}

FP_TEST(Assets_MaterialReimportPreservesUnsavedChanges)
{
	ProjectFixture fixture;
	fixture.Files.Write("Content/Brick.material", "Roughness 0.2\n");
	FP_EXPECT(Paths::GetAssets().Refresh());
	Material* material = MaterialLibrary::Get().Load("Brick.material");
	FP_EXPECT(material != nullptr);
	if (!material) return;
	fixture.Files.Write("Content/Brick.material", "Roughness 0.8\n");
	FP_EXPECT(Paths::GetAssets().Refresh());
	FP_EXPECT(MaterialLibrary::Get().Load("Brick.material") == material);
	FP_EXPECT(material->GetData().Roughness == 0.8f);
	material->GetData().Roughness = 0.4f;
	material->MarkDirty();
	fixture.Files.Write("Content/Brick.material", "Roughness 0.1\n");
	FP_EXPECT(Paths::GetAssets().Refresh());
	FP_EXPECT(MaterialLibrary::Get().Load("Brick.material")->GetData().Roughness == 0.4f);
}

FP_TEST(Assets_GPUReimportRefreshesTexturesAndMeshes)
{
	if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return;
	GPUDevice* device = GPUDevice::Create();
	if (!device) { SDL_QuitSubSystem(SDL_INIT_VIDEO); return; }
	{
		ProjectFixture fixture;
		auto image = [](uint8 width) {
			std::string bytes(18, '\0');
			bytes[2] = 2;
			bytes[12] = static_cast<char>(width);
			bytes[14] = 1;
			bytes[16] = 32;
			bytes[17] = 0x28;
			for (uint8 i = 0; i < width; ++i) bytes.append("\x10\x20\x40\xff", 4);
			return bytes;
		};
		fixture.Files.Write("Content/pixel.tga", image(1));
		fixture.Files.Write("Content/mesh.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
		FP_EXPECT(Paths::GetAssets().Refresh());
		TextureCache textures(*device);
		MeshLibrary meshes(*device);
		FP_EXPECT(textures.Preload({ { "pixel.tga", true }, { Paths::AssetReference("pixel.tga"), true } }) == 1);
		GPUTexture* texture = textures.Get("pixel.tga", true);
		FP_EXPECT(texture && texture->GetWidth() == 1);
		const GPUMesh* mesh = meshes.Get("mesh.obj");
		FP_EXPECT(mesh && mesh->BoundsMax.X == 1);
		fixture.Files.Write("Content/pixel.tga", image(2));
		fixture.Files.Write("Content/mesh.obj", "v 0 0 0\nv 3 0 0\nv 0 1 0\nf 1 2 3\n");
		FP_EXPECT(Paths::GetAssets().Refresh());
		texture = textures.Get("pixel.tga", true);
		FP_EXPECT(texture && texture->GetWidth() == 2);
		mesh = meshes.Get("mesh.obj");
		FP_EXPECT(mesh && mesh->BoundsMax.X == 3);
		FP_EXPECT(Paths::GetAssets().SetImportSettings("pixel.tga", { false, AssetColorSpace::Linear }));
		texture = textures.Get("pixel.tga", true);
		FP_EXPECT(texture && texture->GetWidth() == 2);
		const String reference = Paths::AssetReference("pixel.tga");
		String error;
		FP_EXPECT(Paths::GetAssets().Move("pixel.tga", "renamed.tga", error));
		texture = textures.Get(reference, true);
		FP_EXPECT(texture && texture->GetWidth() == 2);
	}
	delete device;
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

FP_TEST(Assets_GLBBuffersCanExceedTheDocumentLimit)
{
	AssetsFixture fixture;
	fixture.Write("mesh.bin", "data");
	std::string json = "{\"buffers\":[{\"uri\":\"mesh.bin\"}]}";
	while (json.size() % 4) json += ' ';
	const uint32 total = 40 * 1024 * 1024;
	std::ofstream file(fixture.Root / "mesh.glb", std::ios::binary);
	auto word = [&](uint32 value) {
		for (int i = 0; i < 4; ++i) file.put(static_cast<char>((value >> (i * 8)) & 255));
	};
	word(0x46546c67); word(2); word(total);
	word(static_cast<uint32>(json.size())); word(0x4e4f534a);
	file.write(json.data(), static_cast<std::streamsize>(json.size()));
	word(total - 28 - static_cast<uint32>(json.size())); word(0x004e4942);
	file.seekp(total - 1);
	file.put('\0');
	file.close();
	FP_EXPECT(fixture.Database.Open(fixture.Path(), true));
	const auto* asset = fixture.Database.Find("mesh.glb");
	FP_EXPECT(asset && asset->Dependencies.Contains("mesh.bin"));
}

FP_TEST(Assets_DependencyCyclesTerminateAndStillInvalidate)
{
	AssetsFixture fixture;
	fixture.Write("one.js", "import './two.js'");
	fixture.Write("two.js", "import './one.js'");
	FP_EXPECT(fixture.Database.Open(fixture.Path(), true));
	const uint64 before = fixture.Database.Fingerprint("one.js");
	fixture.Write("two.js", "import './one.js'\n// changed");
	FP_EXPECT(fixture.Database.Refresh());
	FP_EXPECT(fixture.Database.Fingerprint("one.js") != before);
}

FP_TEST(Assets_UIPagesResolveIdsAndReimportExternalResources)
{
	if (!UIRuntime::Get().IsAvailable()) return;
	ProjectFixture fixture;
	fixture.Files.Write("Content/UI/page.html", "<html><head><script src=\"../Shared/logic.js\"></script></head><body>Assets</body></html>");
	fixture.Files.Write("Content/Shared/logic.js", "document.title = 'before'");
	FP_EXPECT(Paths::GetAssets().Refresh());
	World world;
	const Entity entity = world.CreateEntity("Page");
	world.Add<UIComponent>(entity).Page = Paths::AssetReference("UI/page.html");
	world.AddSystem<UISystem>();
	world.BeginPlay();
	world.Update(0.016f);
	UIDocumentInstance* document = UISystem::For(world, entity);
	auto pump = [&](const char* title) {
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (std::chrono::steady_clock::now() < deadline)
		{
			world.Update(0.016f);
			if (document->IsReady() && document->RunScript("document.title") == title) return true;
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		return false;
	};
	FP_EXPECT(pump("before"));
	fixture.Files.Write("Content/Shared/logic.js", "document.title = 'after'");
	FP_EXPECT(Paths::GetAssets().Refresh());
	world.Update(0.6f);
	FP_EXPECT(pump("after"));
	world.EndPlay();
}

FP_TEST(Assets_ScriptAssetFieldsParticipateInValidationAndMoves)
{
	AssetsFixture fixture;
	fixture.Write("image.png", "image");
	fixture.Write("main.scene", "component Scripts\n  script AssetFieldScript\n    Image image.png\n");
	FP_EXPECT(fixture.Database.Open(fixture.Path(), true));
	FP_EXPECT(fixture.Database.Dependents("image.png").Contains("main.scene"));
	String error;
	FP_EXPECT(fixture.Database.Move("image.png", "moved.png", error));
	FP_EXPECT(fixture.Read("main.scene").Contains("asset://"));
	FP_EXPECT(fixture.Database.GetIssues().IsEmpty());
}

FP_TEST(Assets_NewMaterialsKeepTheirIdentityWhenPathsAreReused)
{
	ProjectFixture fixture;
	Material* material = MaterialLibrary::Get().Create("original.material");
	FP_EXPECT(material != nullptr);
	if (!material) return;
	String error;
	FP_EXPECT(Paths::GetAssets().Move("original.material", "moved.material", error));
	fixture.Files.Write("Content/original.material", "Roughness 0.1\n");
	FP_EXPECT(Paths::GetAssets().Refresh());
	material->GetData().Roughness = 0.7f;
	material->MarkDirty();
	FP_EXPECT(material->Save());
	Material moved("moved.material");
	FP_EXPECT(moved.Load() && moved.GetData().Roughness == 0.7f);
	FP_EXPECT(fixture.Files.Read("Content/original.material") == "Roughness 0.1\n");
}

FP_TEST(Assets_MovesCannotFollowSymlinksOutsideContent)
{
	AssetsFixture fixture;
	AssetsFixture outside;
	outside.Write("nested/file.bin", "outside");
	std::error_code ec;
	fs::create_directory_symlink(outside.Root, fixture.Root / "Link", ec);
	if (ec) return;
	FP_EXPECT(!fixture.Database.Open(fixture.Path(), true));
	String error;
	FP_EXPECT(!fixture.Database.Move("Link/nested", "Imported", error));
	FP_EXPECT(fs::exists(outside.Root / "nested/file.bin"));
	FP_EXPECT(!fs::exists(fixture.Root / "Imported"));
}

FP_TEST(Assets_IncrementalRefreshSkipsUnchangedSourceBytes)
{
	AssetsFixture fixture;
	fixture.Write("source.bin", std::string(1024 * 1024, 'x'));
	fixture.Write("main.material", "BaseColorMap source.bin\n");
	FP_EXPECT(fixture.Database.Open(fixture.Path(), true));
	FP_EXPECT(fixture.Database.GetLastScanStats().FilesHashed == 2);
	const uint64 fingerprint = fixture.Database.Fingerprint("main.material");
	FP_EXPECT(fixture.Database.Refresh(AssetRefreshMode::ChangedFiles));
	FP_EXPECT(fixture.Database.GetLastScanStats().FilesHashed == 0);
	FP_EXPECT(fixture.Database.GetLastScanStats().BytesHashed == 0);
	FP_EXPECT(fixture.Database.Fingerprint("main.material") == fingerprint);
	fixture.Write("source.bin", "new content");
	FP_EXPECT(fixture.Database.Refresh(AssetRefreshMode::ChangedFiles));
	FP_EXPECT(fixture.Database.GetLastScanStats().FilesHashed == 1);
	FP_EXPECT(fixture.Database.Fingerprint("main.material") != fingerprint);
	fs::remove(fixture.Root / "source.bin");
	FP_EXPECT(!fixture.Database.Refresh(AssetRefreshMode::ChangedFiles));
}

FP_TEST(Assets_NotificationsAndVerificationCatchPreservedTimestamps)
{
	AssetsFixture fixture;
	fixture.Write("source.bin", "old");
	FP_EXPECT(fixture.Database.Open(fixture.Path(), true));
	const auto stamp = fs::last_write_time(fixture.Root / "source.bin");
	const uint64 before = fixture.Database.Fingerprint("source.bin");
	fixture.Write("source.bin", "new");
	fs::last_write_time(fixture.Root / "source.bin", stamp);
	fixture.Database.Invalidate("source.bin");
	FP_EXPECT(fixture.Database.Refresh(AssetRefreshMode::ChangedFiles));
	FP_EXPECT(fixture.Database.GetLastScanStats().FilesHashed == 1);
	FP_EXPECT(fixture.Database.Fingerprint("source.bin") != before);
	const uint64 changed = fixture.Database.Fingerprint("source.bin");
	fixture.Write("source.bin", "end");
	fs::last_write_time(fixture.Root / "source.bin", stamp);
	FP_EXPECT(fixture.Database.Refresh());
	FP_EXPECT(fixture.Database.Fingerprint("source.bin") != changed);
}

FP_TEST(Assets_IncrementalRefreshStillValidatesMetadataAndDependencies)
{
	AssetsFixture fixture;
	fixture.Write("image.png", "image");
	FP_EXPECT(fixture.Database.Open(fixture.Path(), true));
	AssetImportSettings settings{ false, AssetColorSpace::Linear };
	FP_EXPECT(fixture.Database.SetImportSettings("image.png", settings));
	FP_EXPECT(fixture.Database.Refresh(AssetRefreshMode::ChangedFiles));
	const auto* asset = fixture.Database.Find("image.png");
	FP_EXPECT(asset && asset->Settings == settings);
	fixture.Write("image.png.fpmeta", "version 99\n");
	FP_EXPECT(!fixture.Database.Refresh(AssetRefreshMode::ChangedFiles));
	FP_EXPECT(fixture.Database.Find("image.png") == nullptr);
}
