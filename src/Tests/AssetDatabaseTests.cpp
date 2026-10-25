#include "Tests/TestFramework.h"
#include "Engine/Assets/AssetDatabase.h"
#include "Engine/Engine.h"
#include "Engine/Paths.h"
#include "Engine/Project.h"
#include "Engine/SceneSerializer.h"
#include "Engine/Material.h"
#include "Engine/Render/TextureCache.h"
#include "Engine/Render/MeshLibrary.h"
#include <SDL3/SDL.h>
#include "Core/Misc/File.h"

#include <filesystem>
#include <fstream>

using namespace FP;

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
	fixture.Write("Models/mesh.gltf", "{\"buffers\":[{\"uri\":\"mesh.bin\"}],\"images\":[{\"uri\":\"../image.png\"},{\"uri\":\"data:image/png;base64,AAAA\"}]}");
	fixture.Write("Models/mesh.bin", "buffer");
	fixture.Write("image.png", "image");
	fixture.Write("UI/main.html", "<link href=\"style.css?v=2\"><script src=\"script.js\"></script><a href=\"https://example.com\">link</a>");
	fixture.Write("UI/style.css", "body { background: url('../image.png') }");
	fixture.Write("UI/script.js", "import './helper.js'");
	fixture.Write("UI/helper.js", "");
	FP_EXPECT(fixture.Database.Open(fixture.Path(), true));
	const auto* model = fixture.Database.Find("Models/mesh.gltf");
	FP_EXPECT(model && model->Dependencies.Num() == 2);
	const auto* page = fixture.Database.Find("UI/main.html");
	FP_EXPECT(page && page->Dependencies.Num() == 2);
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
