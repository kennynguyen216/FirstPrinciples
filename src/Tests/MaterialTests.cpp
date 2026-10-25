#include "Tests/TestFramework.h"
#include "Tests/TestProject.h"
#include "Engine/Engine.h"
#include "Engine/Material.h"
#include "Engine/Render/Renderer.h"

#include <SDL3/SDL.h>

#include <cstring>
#include <set>

using namespace FP;

FP_TEST(Material_CelShadedRoundTripAndLegacyDefaults)
{
	Material material("Test.material");
	FP_EXPECT(!material.GetData().CelShaded);
	for (const char* shader : Material::GetShaderNames())
	{
		material.GetData().Shader = shader;
		material.GetData().CelShaded = true;
		Material copy("Copy.material");
		copy.LoadFromString(material.SaveToString().View());
		FP_EXPECT(copy.GetData().Shader == shader && copy.GetData().CelShaded);
		copy.GetData().CelShaded = false;
		material.LoadFromString(copy.SaveToString().View());
		FP_EXPECT(!material.GetData().CelShaded);

		// Loading an older file must also clear a previously enabled toggle.
		material.GetData().CelShaded = true;
		material.LoadFromString("Shader Lit\nRoughness 0.7\n");
		FP_EXPECT(!material.GetData().CelShaded && material.GetData().Roughness == 0.7f);
	}
}

FP_TEST(Material_CelShadedGPUAllShadersToggleLiveAndPreserveEmission)
{
	if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return;
	GPUDevice* device = GPUDevice::Create();
	if (!device) { SDL_QuitSubSystem(SDL_INIT_VIDEO); return; }
	Engine::RegisterEngineComponents();
	{
		Test::ScopedProject project;
		Material* material = MaterialLibrary::Get().Create("Cel.material");
		FP_EXPECT(material);
		if (material)
		{
			material->MarkDirty();
			Renderer renderer(*device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB);
			FP_EXPECT(renderer.IsValid());
			constexpr uint32 size = 64;
			auto target = device->CreateRenderTarget(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB, size, size, false);
			FP_EXPECT(target);
			World world;
			const Entity sphere = world.CreateEntity("Cel sphere");
			world.Add<MeshComponent>(sphere).Mesh = "Sphere";
			world.Add<MaterialComponent>(sphere).Material = material->GetPath();
			const Entity sun = world.CreateEntity("Sun");
			world.Get<TransformComponent>(sun).Rotation.Y = 35;
			auto& light = world.Add<DirectionalLightComponent>(sun);
			light.Intensity = 0.8f;
			light.CastShadows = false;
			RenderView view = RenderView::Orthographic2D({}, 48, size, size);
			view.Background = { 0, 0, 0, 1 };
			view.Tonemap = false;
			auto render = [&](uint8* pixels) {
				auto* cmd = SDL_AcquireGPUCommandBuffer(device->GetHandle());
				renderer.Render(cmd, target->GetHandle(), size, size, world, view);
				FP_EXPECT(SDL_SubmitGPUCommandBuffer(cmd));
				FP_EXPECT(device->ReadPixels(target->GetHandle(), size, size, pixels));
			};
			if (target && renderer.IsValid())
			{
				uint8 smooth[size * size * 4] = {}, cel[sizeof(smooth)] = {}, restored[sizeof(smooth)] = {};
				for (const char* shader : Material::GetShaderNames())
				{
					auto& data = material->GetData();
					data = MaterialData();
					data.Shader = shader;
					data.BaseColor = { 0.63f, 0.63f, 0.63f, 1 };
					data.Specular = 0;
					render(smooth);
					data.CelShaded = true;
					render(cel);
					int32 changed = 0;
					std::set<uint8> smoothTones, celTones;
					for (uint32 i = 0; i < sizeof(smooth); i += 4)
					{
						changed += smooth[i] != cel[i];
						smoothTones.insert(smooth[i]);
						celTones.insert(cel[i]);
					}
					FP_EXPECT(changed > 100);
					if (data.Shader == "Lit")
						FP_EXPECT(smoothTones.size() > celTones.size() * 2);
					data.CelShaded = false;
					render(restored);
					FP_EXPECT(std::memcmp(smooth, restored, sizeof(smooth)) == 0);

					// Emission remains identical even along the dark silhouette.
					light.Intensity = 0;
					data.BaseColor = { 0, 0, 0, 1 };
					data.EmissiveColor = { 0.35f, 0.15f, 0.05f, 1 };
					render(smooth);
					data.CelShaded = true;
					render(cel);
					FP_EXPECT(std::memcmp(smooth, cel, sizeof(smooth)) == 0);
					light.Intensity = 0.8f;
				}
			}
		}
		MaterialLibrary::Get().Clear();
	}
	delete device;
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
