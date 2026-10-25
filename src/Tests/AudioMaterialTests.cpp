#include "Tests/TestFramework.h"
#include "Engine/Engine.h"
#include "Engine/Material.h"
#include "Engine/Physics/PhysicsSystem.h"

#include <cmath>
#include <limits>

using namespace FP;

namespace
{
	struct EditedMaterial
	{
		Material* Asset;
		MaterialData Saved;
		explicit EditedMaterial(const char* path) : Asset(MaterialLibrary::Get().Load(path)), Saved(Asset ? Asset->GetData() : MaterialData{}) { FP_EXPECT(Asset); }
		~EditedMaterial() { if (Asset) Asset->GetData() = Saved; }
	};
	Entity Wall(World& world, Vec3 position, Vec3 size, const char* material)
	{
		const Entity entity = world.CreateEntity("Acoustic surface");
		world.Get<TransformComponent>(entity).Position = position;
		world.Add<ColliderComponent>(entity).Size = size;
		world.Add<MaterialComponent>(entity).Material = material;
		return entity;
	}
}

FP_TEST(AudioMaterial_ScalarsRoundTripAndLegacyDefaults)
{
	Material material("Test.material");
	material.LoadFromString("SoundTransmission 0.8\nSoundAbsorption 0.2\nSoundDamping 0.1\n");
	FP_EXPECT(material.GetData().SoundTransmission == 0.8f);
	Material back("Back.material"); back.LoadFromString(material.SaveToString().View());
	FP_EXPECT(back.GetData().SoundTransmission == 0.8f && back.GetData().SoundAbsorption == 0.2f && back.GetData().SoundDamping == 0.1f);
	material.LoadFromString("Shader Lit\nRoughness 0.7\n");
	FP_EXPECT(material.GetData().SoundTransmission == 0.25f && material.GetData().SoundAbsorption == 0.4f && material.GetData().SoundDamping == 0.5f);
	FP_EXPECT(AudioDampingCutoff(0) == 0 && AudioDampingCutoff(0.5f) == 1200 && AudioDampingCutoff(1) < 100);
}

FP_TEST(AudioMaterial_TransmissionLayersLiveEditsParentsAndInvalidValues)
{
	Engine::RegisterEngineComponents();
	EditedMaterial a("Materials/Acoustics/Soft.material"), b("Materials/Acoustics/Bright.material");
	if (!a.Asset || !b.Asset) return;
	auto& soft = a.Asset->GetData(); auto& bright = b.Asset->GetData();
	soft.SoundTransmission = 0.8f; soft.SoundDamping = 0.2f;
	bright.SoundTransmission = 0.5f; bright.SoundDamping = 0.8f;
	StrongObjectPtr<World> world(NewObject<World>());
	const Entity listener = world->CreateEntity("Listener"); world->Add<AudioListenerComponent>(listener);
	const Entity first = Wall(*world.Get(), {2, 0, 0}, {0.2f, 4, 4}, "Materials/Acoustics/Soft.material");
	Wall(*world.Get(), {5, 0, 0}, {0.2f, 4, 4}, "Materials/Acoustics/Bright.material");
	const Entity trigger = Wall(*world.Get(), {3, 0, 0}, {0.2f, 4, 4}, "Materials/Acoustics/Bright.material");
	world->Get<ColliderComponent>(trigger).Trigger = true;
	world->AddSystem<PhysicsSystem>(); world->BeginPlay();
	OneShotParams params; params.Spatial = true; params.Position = {8, 0, 0}; params.Occlusion = false;
	const float clear = world->GetAudio().GetSpatialParameters(params).RightGain;
	params.Occlusion = true;
	const auto layers = world->GetAudio().GetSpatialParameters(params);
	FP_EXPECT(std::abs(layers.RightGain - clear * 0.4f) < 0.00001f);
	FP_EXPECT(std::abs(layers.LowPassHz - AudioDampingCutoff(0.8f)) < 0.001f);
	const auto hits = world->GetPhysics().RaycastAll({}, {1, 0, 0}, 8);
	FP_EXPECT(hits.Num() == 2 && hits[0].Entity == first && hits[0].Distance < hits[1].Distance);
	FP_EXPECT(world->GetPhysics().RaycastAll({}, {}, 8).IsEmpty());
	FP_EXPECT(world->GetPhysics().RaycastAll({}, {1, 0, 0}, 8, hits[0].Entity, hits[1].Entity).IsEmpty());
	soft.SoundTransmission = bright.SoundTransmission = 1;
	soft.SoundDamping = bright.SoundDamping = 0;
	const auto transparent = world->GetAudio().GetSpatialParameters(params);
	FP_EXPECT(transparent.RightGain == clear && transparent.LowPassHz == 0);
	bright.SoundTransmission = 0;
	FP_EXPECT(world->GetAudio().GetSpatialParameters(params).RightGain == 0);
	bright.SoundTransmission = 1;
	soft.SoundTransmission = std::numeric_limits<float>::infinity(); soft.SoundDamping = std::numeric_limits<float>::quiet_NaN();
	const auto invalid = world->GetAudio().GetSpatialParameters(params);
	FP_EXPECT(std::abs(invalid.RightGain - clear * 0.25f) < 0.00001f && invalid.LowPassHz == 1200);
	soft.SoundTransmission = 2; soft.SoundDamping = -1;
	FP_EXPECT(world->GetAudio().GetSpatialParameters(params).RightGain == clear);
	const Entity parent = world->CreateEntity("Parent material");
	world->Add<MaterialComponent>(parent).Material = "Materials/Acoustics/Soft.material";
	world->Remove<MaterialComponent>(first); world->SetParent(first, parent, true);
	soft.SoundTransmission = 0.3f;
	FP_EXPECT(std::abs(world->GetAudio().GetSpatialParameters(params).RightGain - clear * 0.3f) < 0.00001f);
	auto& slots = world->Add<MaterialComponent>(first); slots.Slots.Add("Materials/Acoustics/Bright.material");
	FP_EXPECT(world->GetAudio().GetSpatialParameters(params).RightGain == clear);
	world->EndPlay();
}

FP_TEST(AudioMaterial_PerSurfaceReflectionsAndRoomTail)
{
	Engine::RegisterEngineComponents();
	EditedMaterial a("Materials/Acoustics/Bright.material"), b("Materials/Acoustics/Soft.material");
	if (!a.Asset || !b.Asset) return;
	a.Asset->GetData().SoundAbsorption = 0; a.Asset->GetData().SoundDamping = 0.1f;
	b.Asset->GetData().SoundAbsorption = 0.9f; b.Asset->GetData().SoundDamping = 0.9f;
	StrongObjectPtr<World> world(NewObject<World>());
	const Entity listener = world->CreateEntity("Listener"); world->Add<AudioListenerComponent>(listener);
	const Entity room = world->CreateEntity("Room");
	auto& settings = world->Add<AudioRoomComponent>(room); settings.Size = {8, 4, 10}; settings.Decay = 3;
	const Vec3 positions[] = {{-4.1f, 0, 0}, {4.1f, 0, 0}, {0, -2.1f, 0}, {0, 2.1f, 0}, {0, 0, -5.1f}, {0, 0, 5.1f}};
	Entity walls[6];
	for (int i = 0; i < 6; ++i)
	{
		const Vec3 size = i < 2 ? Vec3{0.2f, 4.4f, 10.4f} : i < 4 ? Vec3{8.4f, 0.2f, 10.4f} : Vec3{8.4f, 4.4f, 0.2f};
		walls[i] = Wall(*world.Get(), positions[i], size, "Materials/Acoustics/Bright.material");
	}
	world->AddSystem<PhysicsSystem>(); world->BeginPlay();
	OneShotParams params; params.Spatial = true; params.MaxDistance = 100;
	world->GetAudio().SetDebugEnabled(true);
	const auto bright = world->GetAudio().GetSpatialParameters(params);
	const auto& debug = world->GetAudio().GetDebugSources();
	FP_EXPECT(debug.size() == 1 && debug[0].Rays.size() == 13 && debug[0].Hits.size() == 6);
	world->Get<MaterialComponent>(walls[0]).Material = "Materials/Acoustics/Soft.material";
	const auto mixed = world->GetAudio().GetSpatialParameters(params);
	FP_EXPECT(std::abs(mixed.Room.Reflections[0].LeftGain - bright.Room.Reflections[0].LeftGain * 0.1f) < 0.00001f);
	FP_EXPECT(mixed.Room.Reflections[0].Damping == 0.9f && mixed.Room.Reflections[1].Damping == 0.1f);
	FP_EXPECT(mixed.Room.Reflections[1].RightGain == bright.Room.Reflections[1].RightGain);
	FP_EXPECT(mixed.Room.Decay < bright.Room.Decay && mixed.Room.Damping > bright.Room.Damping && mixed.Room.LateGain < bright.Room.LateGain);
	for (Entity wall : walls) world->Get<MaterialComponent>(wall).Material = "Materials/Acoustics/Soft.material";
	b.Asset->GetData().SoundAbsorption = 1;
	const auto absorbed = world->GetAudio().GetSpatialParameters(params);
	FP_EXPECT(absorbed.Room.LateGain == 0);
	for (const auto& reflection : absorbed.Room.Reflections) FP_EXPECT(reflection.LeftGain == 0 && reflection.RightGain == 0);
	world->EndPlay();
}
