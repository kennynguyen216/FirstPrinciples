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

		registry.Register<AudioDemoComponent>("AudioDemo")
			.Field("Clip", &AudioDemoComponent::Clip).AssetPath("Audio (*.wav *.mp3)");
		registry.Register<AcousticsTestComponent>("AcousticsTest")
			.Field("AutoClap", &AcousticsTestComponent::AutoClap);
		registry.Register<MovingAudioComponent>("MovingAudio")
			.Field("Distance", &MovingAudioComponent::Distance).Range(0.1f, 100)
			.Field("Speed", &MovingAudioComponent::Speed).Range(0, 150);
	}

	void AddSystems(World& world)
	{
		world.AddSystem<PlayerSystem>();
		world.AddSystem<SpinnerSystem>();
		world.AddSystem<FollowPlayerSystem>();
		world.AddSystem<AudioDemoSystem>();
		world.AddSystem<AcousticsTestSystem>();
	}
}
