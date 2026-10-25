#include "Engine/Engine.h"
#include "Engine/Assets/AssetDatabase.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace FP;

namespace
{
	double Sink = 0;

	template<typename Work>
	void Measure(const char* name, int iterations, Work work)
	{
		std::vector<double> samples;
		for (int repeat = 0; repeat < 7; ++repeat)
		{
			const auto start = std::chrono::steady_clock::now();
			for (int i = 0; i < iterations; ++i) work();
			samples.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / iterations);
		}
		std::sort(samples.begin(), samples.end());
		std::printf("%s,%.6f\n", name, samples[samples.size() / 2]);
	}
}

int main()
{
	Engine::RegisterEngineComponents();
	std::puts("benchmark,median_ms_per_operation");
	{
		World world;
		Entity leaf = NullEntity;
		for (int i = 0; i < 128; ++i)
		{
			const Entity next = world.CreateEntity("Node");
			world.Get<TransformComponent>(next).Position = { 1, 0, 0 };
			world.SetParent(next, leaf, false);
			leaf = next;
		}
		Measure("hierarchy_matrix_depth_128", 200, [&] { Sink += world.GetWorldPosition(leaf).X; });
		Measure("hierarchy_rotation_depth_128", 200, [&] { Sink += world.GetWorldRotation(leaf).W; });
	}
	{
		World world;
		const Entity root = world.CreateEntity("Root");
		for (int i = 0; i < 10000; ++i)
		{
			const Entity entity = world.CreateEntity("Node");
			if (i < 16) world.SetParent(entity, root, false);
		}
		Measure("entity_order_10000", 100, [&] { Sink += world.GetEntities().Num(); });
		Measure("children_16_of_10000", 100, [&] { Sink += world.GetChildren(root).Num(); });
	}
	namespace fs = std::filesystem;
	const fs::path root = fs::temp_directory_path() / ("FPBenchmark-" + std::string(Guid::New().ToString().View()));
	fs::create_directories(root);
	{
		std::ofstream large(root / "large.bin", std::ios::binary);
		const std::string block(1024 * 1024, 'x');
		for (int i = 0; i < 64; ++i) large.write(block.data(), block.size());
	}
	for (int i = 0; i < 128; ++i)
	{
		std::ofstream material(root / (std::to_string(i) + ".material"));
		material << "BaseColorMap \"large.bin\"\n";
	}
	{
		AssetDatabase assets;
		if (!assets.Open(String(root.generic_string()), true)) return 1;
		Measure("asset_refresh_64_mib_129_assets", 3, [&] { Sink += assets.Refresh(); });
		const String reference = assets.Reference("large.bin");
		Measure("asset_id_lookup", 100000, [&] { Sink += assets.Fingerprint(reference) != 0; });
	}
	std::error_code ec;
	fs::remove_all(root, ec);
	return Sink == 0;
}
