#include "Engine/Engine.h"
#include "Engine/Paths.h"
#include "Engine/FieldSerializer.h"
#include "Engine/Net/NetworkComponent.h"
#include "Engine/Render/MeshData.h"
#include "Engine/Scripting/ScriptComponent.h"

namespace FP::Engine
{
	const char* const ImageFilter = "Images (*.png *.jpg *.jpeg *.tga *.bmp)";
	const char* const MeshFilter = "Meshes (*.obj *.gltf *.glb)";
	const char* const SkeletalMeshFilter = "Skeletal meshes (*.gltf *.glb)";

	namespace
	{
		// Slots are written only when set, as "Slot index path", so one material stays one line.
		constexpr int32 MaxMaterialSlots = 256;

		void WriteParentComponent(String& out, const void* component)
		{
			out.Appendf("  Parent %s\n", static_cast<const ParentComponent*>(component)->Parent.ToString().CStr());
		}

		void ReadParentComponent(void* component, const Array<String>& tokens, std::string_view)
		{
			Guid id;
			if (tokens[0] == "Parent" && tokens.Num() > 1 && Guid::Parse(tokens[1].View(), id))
				static_cast<ParentComponent*>(component)->Parent = id;
			else
				FP_LOG_WARN("Parent component needs a parent id");
		}

		void WriteMaterialComponent(String& out, const void* component)
		{
			const MaterialComponent& material = *static_cast<const MaterialComponent*>(component);
			out += "  Material ";
			FieldSerializer::AppendQuoted(out, Paths::AssetReference(material.Material));
			out += '\n';
			for (int32 i = 0; i < material.Slots.Num(); ++i)
			{
				if (material.Slots[i].IsEmpty())
					continue;
				out.Appendf("  Slot %d ", i);
				FieldSerializer::AppendQuoted(out, Paths::AssetReference(material.Slots[i]));
				out += '\n';
			}
		}

		void ReadMaterialComponent(void* component, const Array<String>& tokens, std::string_view)
		{
			MaterialComponent& material = *static_cast<MaterialComponent*>(component);
			int64 index = 0;
			if (tokens[0] == "Material" && tokens.Num() > 1)
				material.Material = Paths::AssetReference(tokens[1]);
			else if (tokens[0] == "Slot" && tokens.Num() > 2 && tokens[1].ToInt(index) && index >= 0 && index < MaxMaterialSlots)
			{
				if (material.Slots.Num() <= index)
					material.Slots.Resize(static_cast<int32>(index) + 1);
				material.Slots[static_cast<int32>(index)] = Paths::AssetReference(tokens[2]);
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

		registry.Register<ParentComponent>("Parent")
			.CustomSerializer(&WriteParentComponent, &ReadParentComponent)
			.HideInEditor();

		registry.Register<SceneComponent>("Scene").HideInEditor();

		registry.Register<NameComponent>("Name")
			.Field("Name", &NameComponent::Name);

		static const Array<const char*> audioBuses{ "SFX", "Music", "UI", "Voice" };
		registry.Register<AudioSourceComponent>("AudioSource")
			.Field("Clip", &AudioSourceComponent::Clip).AssetPath("Audio (*.wav *.mp3)")
			.Field("Enabled", &AudioSourceComponent::Enabled)
			.Field("Volume", &AudioSourceComponent::Volume).Range(0, 1)
			.Field("Pitch", &AudioSourceComponent::Pitch).Range(0.25f, 4)
			.Field("Loop", &AudioSourceComponent::Loop)
			.Field("PlayOnStart", &AudioSourceComponent::PlayOnStart)
			.Field("Bus", &AudioSourceComponent::Bus).Choices(audioBuses)
			.Field("Spatial", &AudioSourceComponent::Spatial)
			.Field("DopplerScale", &AudioSourceComponent::DopplerScale).Range(0, 4).Tooltip("Spatial pitch shift from motion: 0 disables it, 1 uses meters and seconds, higher values exaggerate it")
			.Field("Occlusion", &AudioSourceComponent::Occlusion).Tooltip("Spatial sounds are quieter and muffled behind solid colliders; requires PhysicsSystem during Play")
			.Field("MinDistance", &AudioSourceComponent::MinDistance).Range(0.001f, 1e9f)
			.Field("MaxDistance", &AudioSourceComponent::MaxDistance).Range(0.001f, 1e9f)
			.Field("Priority", &AudioSourceComponent::Priority).Range(0, 255).Tooltip("Higher priority voices keep playing when too many sounds play at once");
		registry.Register<AudioListenerComponent>("AudioListener")
			.Field("Enabled", &AudioListenerComponent::Enabled);
		registry.Register<AudioRoomComponent>("AudioRoom")
			.Field("Enabled", &AudioRoomComponent::Enabled)
			.Field("Size", &AudioRoomComponent::Size).Tooltip("Box interior dimensions in meters; place solid walls at its boundaries")
			.Field("Wet", &AudioRoomComponent::Wet).Range(0, 1)
			.Field("Decay", &AudioRoomComponent::Decay).Range(0.1f, 6).Tooltip("Maximum reverb decay in seconds; surface absorption shortens the tail");

		registry.Register<VoiceChatComponent>("VoiceChat")
			.Field("Enabled", &VoiceChatComponent::Enabled).Tooltip("Player needs Network OnePerPeer; enable microphone explicitly, then hold V to talk")
			.Field("Volume", &VoiceChatComponent::Volume).Range(0, 1)
			.Field("MinDistance", &VoiceChatComponent::MinDistance).Range(0.001f, 1e9f)
			.Field("MaxDistance", &VoiceChatComponent::MaxDistance).Range(0.001f, 1e9f)
			.Field("Occlusion", &VoiceChatComponent::Occlusion).Tooltip("Muffles voices behind solid colliders; requires PhysicsSystem during Play");

		registry.Register<TransformComponent>("Transform")
			.Field("Position", &TransformComponent::Position)
			.Field("Rotation", &TransformComponent::Rotation)
			.Field("Scale", &TransformComponent::Scale);

		registry.Register<SpriteComponent>("Sprite")
			.Field("Tint", &SpriteComponent::Tint)
			.Field("Size", &SpriteComponent::Size)
			.Field("Texture", &SpriteComponent::Texture).AssetPath(ImageFilter)
			.Field("Layer", &SpriteComponent::Layer);

		registry.Register<PostProcessComponent>("PostProcess")
			.Field("FXAA", &PostProcessComponent::FXAA)
			.Field("Bloom", &PostProcessComponent::Bloom)
			.Field("BloomIntensity", &PostProcessComponent::BloomIntensity).Range(0, 10)
			.Field("BloomThreshold", &PostProcessComponent::BloomThreshold).Range(0, 65504);

		registry.Register<CameraComponent>("Camera")
			.Field("Perspective", &CameraComponent::Perspective)
			.Field("Zoom", &CameraComponent::Zoom)
			.Field("FieldOfView", &CameraComponent::FieldOfView).Range(1, 179).Tooltip("Vertical angle in degrees for perspective cameras")
			.Field("NearClip", &CameraComponent::NearClip)
			.Field("FarClip", &CameraComponent::FarClip)
			.Field("Primary", &CameraComponent::Primary)
			.Field("Background", &CameraComponent::Background)
			.Field("Exposure", &CameraComponent::Exposure)
			.Field("Tonemap", &CameraComponent::Tonemap);

		registry.Register<MeshComponent>("Mesh")
			.Field("Mesh", &MeshComponent::Mesh).Choices(Primitives::GetNames(), true).AssetPath(MeshFilter);

		registry.Register<SkeletalMeshComponent>("SkeletalMesh")
			.Field("Mesh", &SkeletalMeshComponent::Mesh).AssetPath(SkeletalMeshFilter);

		registry.Register<AnimatorComponent>("Animator")
			.Field("Clip", &AnimatorComponent::Clip)
			.Field("Speed", &AnimatorComponent::Speed)
			.Field("Playing", &AnimatorComponent::Playing);

		registry.Register<MaterialComponent>("Material")
			.Field("Material", &MaterialComponent::Material).AssetPath("Materials (*.material)")
			.CustomSerializer(&WriteMaterialComponent, &ReadMaterialComponent);

		registry.Register<UIComponent>("UI")
			.Field("Page", &UIComponent::Page).AssetPath("Pages (*.html *.htm)")
			.Field("Visible", &UIComponent::Visible)
			.Field("Input", &UIComponent::Input).Tooltip("Gets the mouse and keyboard while drawn")
			.Field("Order", &UIComponent::Order).Tooltip("Higher draws on top");

		registry.Register<ScriptComponent>("Scripts")
			.CustomSerializer(&ScriptComponentFormat::Write, &ScriptComponentFormat::Read)
			.CustomInspector();

		registry.Register<NetworkComponent>("Network")
			.Field("OnePerPeer", &NetworkComponent::OnePerPeer)
			.Field("SendRate", &NetworkComponent::SendRate);

		static const Array<const char*> emitterShapes{ "Point", "Sphere", "Box" };
		static const Array<const char*> blendModes{ "Additive", "Alpha" };
		registry.Register<ParticleEmitterComponent>("ParticleEmitter")
			.Field("Emitting", &ParticleEmitterComponent::Emitting)
			.Field("Rate", &ParticleEmitterComponent::Rate).Range(0, 1000).Tooltip("Particles per second")
			.Field("Burst", &ParticleEmitterComponent::Burst).Range(0, 1000).Tooltip("Particles all at once when play starts")
			.Field("MaxParticles", &ParticleEmitterComponent::MaxParticles).Range(1, 10000)
			.Field("Lifetime", &ParticleEmitterComponent::Lifetime).Range(0.01f, 30).Tooltip("Seconds each particle lives")
			.Field("LifetimeRandom", &ParticleEmitterComponent::LifetimeRandom).Range(0, 1).Tooltip("Up to this fraction shorter")
			.Field("Shape", &ParticleEmitterComponent::Shape).Choices(emitterShapes)
			.Field("ShapeSize", &ParticleEmitterComponent::ShapeSize).Tooltip("Sphere radius is X, Box is full size")
			.Field("Direction", &ParticleEmitterComponent::Direction).Tooltip("In the entity's space")
			.Field("Spread", &ParticleEmitterComponent::Spread).Range(0, 180).Tooltip("Cone half angle in degrees, 180 sprays every way")
			.Field("Speed", &ParticleEmitterComponent::Speed).Range(0, 100)
			.Field("SpeedRandom", &ParticleEmitterComponent::SpeedRandom).Range(0, 1).Tooltip("Up to this fraction slower")
			.Field("Gravity", &ParticleEmitterComponent::Gravity).Range(-5, 5).Tooltip("1 falls like everything else, negative rises")
			.Field("Drag", &ParticleEmitterComponent::Drag).Range(0, 10)
			.Field("StartSize", &ParticleEmitterComponent::StartSize).Range(0, 10)
			.Field("EndSize", &ParticleEmitterComponent::EndSize).Range(0, 10)
			.Field("StartColor", &ParticleEmitterComponent::StartColor)
			.Field("EndColor", &ParticleEmitterComponent::EndColor)
			.Field("Texture", &ParticleEmitterComponent::Texture).AssetPath(ImageFilter).Tooltip("A soft dot when empty")
			.Field("Blend", &ParticleEmitterComponent::Blend).Choices(blendModes).Tooltip("Additive glows, Alpha covers")
			.Field("WorldSpace", &ParticleEmitterComponent::WorldSpace).Tooltip("Off makes particles move with the entity")
			.Field("DestroyWhenDone", &ParticleEmitterComponent::DestroyWhenDone).Tooltip("Removes the entity once it stops emitting and its particles are gone");

		static const Array<const char*> colliderShapes{ "Box", "Sphere", "Capsule", "Mesh" };
		registry.Register<ColliderComponent>("Collider")
			.Field("Shape", &ColliderComponent::Shape).Choices(colliderShapes)
			.Field("Size", &ColliderComponent::Size).Tooltip("Box size before the transform's scale")
			.Field("Radius", &ColliderComponent::Radius).Tooltip("Sphere and capsule radius")
			.Field("Height", &ColliderComponent::Height).Tooltip("Capsule height from end to end, along Y")
			.Field("Offset", &ColliderComponent::Offset)
			.Field("Trigger", &ColliderComponent::Trigger).Tooltip("Reports overlaps to scripts instead of blocking");

		registry.Register<CharacterControllerComponent>("CharacterController")
			.Field("Radius", &CharacterControllerComponent::Radius).Range(0.05f, 10)
			.Field("Height", &CharacterControllerComponent::Height).Range(0.1f, 20).Tooltip("End to end, standing along Y")
			.Field("StepHeight", &CharacterControllerComponent::StepHeight).Range(0, 2).Tooltip("Steps up to this tall are climbed")
			.Field("MaxSlope", &CharacterControllerComponent::MaxSlope).Range(0, 89).Tooltip("Steepest walkable ground in degrees")
			.Field("Mass", &CharacterControllerComponent::Mass).Range(1, 1000).Tooltip("Kilograms, for pushing things")
			.Field("AirControl", &CharacterControllerComponent::AirControl).Range(0, 1).Tooltip("How much Move steers in the air")
			.Field("GravityScale", &CharacterControllerComponent::GravityScale);

		registry.Register<PlayerInputComponent>("PlayerInput")
			.Field("WalkSpeed", &PlayerInputComponent::WalkSpeed).Range(0, 20)
			.Field("RunSpeed", &PlayerInputComponent::RunSpeed).Range(0, 30).Tooltip("While Shift is held")
			.Field("JumpSpeed", &PlayerInputComponent::JumpSpeed).Range(0, 20)
			.Field("LookSpeed", &PlayerInputComponent::LookSpeed).Range(0, 1).Tooltip("Degrees per pixel of mouse movement, 0 turns mouse look off")
			.Field("Camera", &PlayerInputComponent::Camera).Tooltip("The child camera that tilts up and down");

		static const Array<const char*> motionTypes{ "Dynamic", "Kinematic", "Static" };
		registry.Register<RigidBodyComponent>("RigidBody")
			.Field("Motion", &RigidBodyComponent::Motion).Choices(motionTypes)
			.Field("Mass", &RigidBodyComponent::Mass).Range(0.001f, 100000).Tooltip("Kilograms")
			.Field("Friction", &RigidBodyComponent::Friction).Range(0, 1)
			.Field("Restitution", &RigidBodyComponent::Restitution).Range(0, 1).Tooltip("Bounciness")
			.Field("LinearDamping", &RigidBodyComponent::LinearDamping).Range(0, 1)
			.Field("AngularDamping", &RigidBodyComponent::AngularDamping).Range(0, 1)
			.Field("GravityScale", &RigidBodyComponent::GravityScale)
			.Field("LockRotation", &RigidBodyComponent::LockRotation).Tooltip("Keeps it upright, for characters")
			.Field("Continuous", &RigidBodyComponent::Continuous).Tooltip("Stops fast, small bodies passing through thin walls");

		registry.Register<PointLightComponent>("PointLight")
			.Field("Color", &PointLightComponent::Color)
			.Field("Intensity", &PointLightComponent::Intensity)
			.Field("Range", &PointLightComponent::Range)
			.Field("CastShadows", &PointLightComponent::CastShadows)
			.Field("ShadowResolution", &PointLightComponent::ShadowResolution).Range(64, 2048)
			.Field("ShadowStrength", &PointLightComponent::ShadowStrength).Range(0, 1);

		registry.Register<SpotLightComponent>("SpotLight")
			.Field("Color", &SpotLightComponent::Color)
			.Field("Intensity", &SpotLightComponent::Intensity)
			.Field("Range", &SpotLightComponent::Range)
			.Field("InnerAngle", &SpotLightComponent::InnerAngle).Range(0, 88)
			.Field("OuterAngle", &SpotLightComponent::OuterAngle).Range(1, 89)
			.Field("CastShadows", &SpotLightComponent::CastShadows)
			.Field("ShadowResolution", &SpotLightComponent::ShadowResolution).Range(64, 2048)
			.Field("ShadowStrength", &SpotLightComponent::ShadowStrength).Range(0, 1);

		registry.Register<DirectionalLightComponent>("DirectionalLight")
			.Field("Color", &DirectionalLightComponent::Color)
			.Field("Intensity", &DirectionalLightComponent::Intensity)
			.Field("CastShadows", &DirectionalLightComponent::CastShadows)
			.Field("ShadowResolution", &DirectionalLightComponent::ShadowResolution).Range(64, 4096).Tooltip("Pixels per side. Lower is faster and softer")
			.Field("ShadowDistance", &DirectionalLightComponent::ShadowDistance)
			.Field("ShadowStrength", &DirectionalLightComponent::ShadowStrength).Range(0, 1);

		registry.Register<AmbientLightComponent>("AmbientLight")
			.Field("Color", &AmbientLightComponent::Color)
			.Field("Intensity", &AmbientLightComponent::Intensity);

		registry.Register<SkyboxComponent>("Skybox")
			.Field("Texture", &SkyboxComponent::Texture).AssetPath("Environment panoramas (*.hdr *.png *.jpg *.jpeg *.tga *.bmp)")
			.Field("Intensity", &SkyboxComponent::Intensity).Range(0, 20)
			.Field("Rotation", &SkyboxComponent::Rotation).Range(0, 360)
			.Field("Visible", &SkyboxComponent::Visible)
			.Field("Enabled", &SkyboxComponent::Enabled);

	}
}
