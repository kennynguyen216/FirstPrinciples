#include "Game.h"
#include "GameSystems.h"

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
			.Field("DegreesPerSecond", &SpinnerComponent::DegreesPerSecond)
			.Field("Axis", &SpinnerComponent::Axis);

		registry.Register<FollowPlayerComponent>("FollowPlayer")
			.Field("Smoothing", &FollowPlayerComponent::Smoothing);
	}

	void AddSystems(World& world)
	{
		world.AddSystem<PlayerSystem>();
		world.AddSystem<SpinnerSystem>();
		world.AddSystem<FollowPlayerSystem>();
	}
}
