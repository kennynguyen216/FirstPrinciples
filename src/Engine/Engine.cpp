#include "Engine/Engine.h"

namespace FP::Engine
{
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
			.Field("Texture", &SpriteComponent::Texture)
			.Field("Layer", &SpriteComponent::Layer);

		registry.Register<CameraComponent>("Camera")
			.Field("Zoom", &CameraComponent::Zoom)
			.Field("Primary", &CameraComponent::Primary)
			.Field("Background", &CameraComponent::Background);
	}
}
