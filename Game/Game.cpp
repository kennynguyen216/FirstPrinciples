#include "Game/Game.h"
#include "Game/GameSystems.h"

using namespace FP;

namespace Game
{
	void RegisterComponents()
	{
		ComponentRegistry& registry = ComponentRegistry::Get();
		if (registry.Find<PlayerComponent>())
			return;

		registry.Register<PlayerComponent>("Player")
			.Field("Speed", &PlayerComponent::Speed);

		registry.Register<SpinnerComponent>("Spinner")
			.Field("DegreesPerSecond", &SpinnerComponent::DegreesPerSecond);

		registry.Register<CameraFollowComponent>("CameraFollow")
			.Field("Smoothing", &CameraFollowComponent::Smoothing);
	}

	void AddSystems(World& world)
	{
		world.AddSystem<PlayerSystem>();
		world.AddSystem<SpinnerSystem>();
		world.AddSystem<CameraFollowSystem>();
	}
}
