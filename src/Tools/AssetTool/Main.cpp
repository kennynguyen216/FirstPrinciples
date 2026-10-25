#include "Engine/Assets/AssetDatabase.h"
#include "Engine/Engine.h"
#include "Engine/Paths.h"
#include "Engine/Scripting/ScriptRuntime.h"
#include "Core/Misc/File.h"
#include "Game.h"

#include <cstdio>

int main(int argc, char** argv)
{
	FP::String project;
	FP::String dependencies;
	bool import = false;
	for (int i = 1; i < argc; ++i)
	{
		const std::string_view argument(argv[i]);
		if (argument == "--project" && i + 1 < argc) project = argv[++i];
		else if (argument == "--dependencies" && i + 1 < argc) dependencies = argv[++i];
		else if (argument == "--import") import = true;
		else if (argument != "--validate")
		{
			std::fprintf(stderr, "Usage: AssetTool [--project folder] [--import] [--validate] [--dependencies path]\n");
			return 2;
		}
	}
	if (!project.IsEmpty() && !FP::Paths::OpenProject(project)) return 2;
	FP::Engine::RegisterEngineComponents();
	Game::RegisterComponents();
#if FP_PLATFORM_WINDOWS
	const char* module = "/GameScripts.dll";
#else
	const char* module = "/GameScripts.dylib";
#endif
#if defined(FP_DEBUG)
	const FP::String scripts = FP::Paths::GetProjectDir() + "/Binaries/Debug" + module;
#else
	const FP::String scripts = FP::Paths::GetProjectDir() + "/Binaries/Release" + module;
#endif
	if (FP::File::Exists(scripts.CStr()) && !FP::ScriptRuntime::Get().LoadModule(scripts)) return 1;
	auto& assets = FP::Paths::GetAssets();
	const bool valid = assets.Open(FP::Paths::GetContentDir(), import);
	for (const auto& issue : assets.GetIssues())
		std::fprintf(stderr, "%s: %s\n", issue.Path.CStr(), issue.Message.CStr());
	if (!dependencies.IsEmpty())
	{
		const auto* asset = assets.Find(dependencies);
		if (!asset) { std::fprintf(stderr, "Unknown asset: %s\n", dependencies.CStr()); return 1; }
		for (const auto& reference : asset->Dependencies)
			std::printf("%s\n", assets.Resolve(reference).CStr());
	}
	std::printf("%zu assets, %d issues\n", assets.GetAssets().size(), assets.GetIssues().Num());
	return valid ? 0 : 1;
}
