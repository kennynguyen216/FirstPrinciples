#include "Engine/Engine.h"
#include "Engine/FieldSerializer.h"
#include "Engine/Net/NetworkComponent.h"
#include "Engine/Render/MeshData.h"
#include "Engine/Scripting/ScriptComponent.h"

namespace FP::Engine
{
	const char* const ImageFilter = "Images (*.png *.jpg *.jpeg *.tga *.bmp)";

	namespace
	{
		// Slots are written only when set, as "Slot index path", so one material stays one line.
		constexpr int32 MaxMaterialSlots = 256;

		void WriteMaterialComponent(String& out, const void* component)
		{
			const MaterialComponent& material = *static_cast<const MaterialComponent*>(component);
			out += "  Material ";
			FieldSerializer::AppendQuoted(out, material.Material);
			out += '\n';
			for (int32 i = 0; i < material.Slots.Num(); ++i)
			{
				if (material.Slots[i].IsEmpty())
					continue;
				out.Appendf("  Slot %d ", i);
				FieldSerializer::AppendQuoted(out, material.Slots[i]);
				out += '\n';
			}
		}

		void ReadMaterialComponent(void* component, const Array<String>& tokens, std::string_view)
		{
			MaterialComponent& material = *static_cast<MaterialComponent*>(component);
			int64 index = 0;
			if (tokens[0] == "Material" && tokens.Num() > 1)
				material.Material = tokens[1];
			else if (tokens[0] == "Slot" && tokens.Num() > 2 && tokens[1].ToInt(index) && index >= 0 && index < MaxMaterialSlots)
			{
				if (material.Slots.Num() <= index)
					material.Slots.Resize(static_cast<int32>(index) + 1);
				material.Slots[static_cast<int32>(index)] = tokens[2];
			}
			else
				FP_LOG_WARN("Material component has no field %s", tokens[0].CStr());
		}
	}

	void RegisterEngineComponents()
	{
		ComponentRegistry& registry = ComponentRegistry::Get();
		if (registry.Find<IdComponent>())
			return;

		registry.Register<IdComponent>("Id").HideInEditor();

		registry.Register<NameComponent>("Name")
			.Field("Name", &NameComponent::Name);

		static const Array<const char*> audioBuses{ "SFX", "Music", "UI" };
		registry.Register<AudioSourceComponent>("AudioSource")
			.Field("Clip", &AudioSourceComponent::Clip).AssetPath("Audio (*.wav)")
			.Field("Enabled", &AudioSourceComponent::Enabled)
			.Field("Volume", &AudioSourceComponent::Volume)
			.Field("Pitch", &AudioSourceComponent::Pitch)
			.Field("Loop", &AudioSourceComponent::Loop)
			.Field("PlayOnStart", &AudioSourceComponent::PlayOnStart)
			.Field("Bus", &AudioSourceComponent::Bus).Choices(audioBuses)
			.Field("Spatial", &AudioSourceComponent::Spatial)
			.Field("MinDistance", &AudioSourceComponent::MinDistance)
			.Field("MaxDistance", &AudioSourceComponent::MaxDistance)
			.Field("Priority", &AudioSourceComponent::Priority);
		registry.Register<AudioListenerComponent>("AudioListener")
			.Field("Enabled", &AudioListenerComponent::Enabled);

		registry.Register<TransformComponent>("Transform")
			.Field("Position", &TransformComponent::Position)
			.Field("Rotation", &TransformComponent::Rotation)
			.Field("Scale", &TransformComponent::Scale);

		registry.Register<SpriteComponent>("Sprite")
			.Field("Tint", &SpriteComponent::Tint)
			.Field("Size", &SpriteComponent::Size)
			.Field("Texture", &SpriteComponent::Texture).AssetPath(ImageFilter)
			.Field("Layer", &SpriteComponent::Layer);

		registry.Register<CameraComponent>("Camera")
			.Field("Perspective", &CameraComponent::Perspective)
			.Field("Zoom", &CameraComponent::Zoom)
			.Field("FieldOfView", &CameraComponent::FieldOfView)
			.Field("NearClip", &CameraComponent::NearClip)
			.Field("FarClip", &CameraComponent::FarClip)
			.Field("Primary", &CameraComponent::Primary)
			.Field("Background", &CameraComponent::Background)
			.Field("Exposure", &CameraComponent::Exposure)
			.Field("Tonemap", &CameraComponent::Tonemap);

		registry.Register<MeshComponent>("Mesh")
			.Field("Mesh", &MeshComponent::Mesh).Choices(Primitives::GetNames(), true).AssetPath("Meshes (*.obj)");

		registry.Register<MaterialComponent>("Material")
			.Field("Material", &MaterialComponent::Material).AssetPath("Materials (*.material)")
			.CustomSerializer(&WriteMaterialComponent, &ReadMaterialComponent);

		registry.Register<ScriptComponent>("Scripts")
			.CustomSerializer(&ScriptComponentFormat::Write, &ScriptComponentFormat::Read)
			.CustomInspector();

		registry.Register<NetworkComponent>("Network")
			.Field("OnePerPeer", &NetworkComponent::OnePerPeer)
			.Field("SendRate", &NetworkComponent::SendRate);

		registry.Register<PointLightComponent>("PointLight")
			.Field("Color", &PointLightComponent::Color)
			.Field("Intensity", &PointLightComponent::Intensity)
			.Field("Range", &PointLightComponent::Range);

		registry.Register<DirectionalLightComponent>("DirectionalLight")
			.Field("Color", &DirectionalLightComponent::Color)
			.Field("Intensity", &DirectionalLightComponent::Intensity);

		registry.Register<AmbientLightComponent>("AmbientLight")
			.Field("Color", &AmbientLightComponent::Color)
			.Field("Intensity", &AmbientLightComponent::Intensity);

	}
}
