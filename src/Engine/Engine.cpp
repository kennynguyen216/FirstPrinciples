#include "Engine/Engine.h"

namespace FP::Engine
{
	const char* const ImageFilter = "Images (*.png *.jpg *.jpeg *.tga *.bmp)";

	void RegisterEngineComponents()
	{
		ComponentRegistry& registry = ComponentRegistry::Get();
		if (registry.Find<IdComponent>())
			return;

		registry.Register<IdComponent>("Id").HideInEditor();

		registry.Register<NameComponent>("Name")
			.Field("Name", &NameComponent::Name);

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
			.Field("Background", &CameraComponent::Background);
	}
}
