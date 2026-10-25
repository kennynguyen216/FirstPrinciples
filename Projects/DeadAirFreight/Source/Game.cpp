#include "Game.h"
#include "Audio/Sounds.h"
#include "Crew/OnFootSystem.h"
#include "Enemies/Airlock.h"
#include "Enemies/SiphonSystems.h"
#include "LifeSupport/LifeSupportSystems.h"
#include "Match/MatchSystems.h"
#include "Bridge/DriverBridge.h"

using namespace FP;

namespace Game
{
	void RegisterComponents()
	{
		ComponentRegistry& registry = ComponentRegistry::Get();
		if (registry.Find<MatchConfigComponent>())
			return;

		registry.Register<MatchConfigComponent>("MatchConfig")
			.Field("PlayersRequired", &MatchConfigComponent::PlayersRequired)
			.Field("CountdownSeconds", &MatchConfigComponent::CountdownSeconds)
			.Field("SecondsPerRealDay", &MatchConfigComponent::SecondsPerRealDay)
			.Field("ColonyCrewSize", &MatchConfigComponent::ColonyCrewSize)
			.Field("ColonyOxygenDays", &MatchConfigComponent::ColonyOxygenDays)
			.Field("CargoOxygenTanks", &MatchConfigComponent::CargoOxygenTanks)
			.Field("CargoWaterTanks", &MatchConfigComponent::CargoWaterTanks)
			.Field("ParTimeSeconds", &MatchConfigComponent::ParTimeSeconds)
			.Field("TimeBonusPerSecond", &MatchConfigComponent::TimeBonusPerSecond)
			.Field("StatusLogSeconds", &MatchConfigComponent::StatusLogSeconds);

		registry.Register<LifeSupportDataComponent>("LifeSupportData")
			.Field("OxygenKgPerPersonDay", &LifeSupportDataComponent::OxygenKgPerPersonDay)
			.Field("CarbonDioxideKgPerPersonDay", &LifeSupportDataComponent::CarbonDioxideKgPerPersonDay)
			.Field("WaterKgPerPersonDay", &LifeSupportDataComponent::WaterKgPerPersonDay)
			.Field("IssWaterRecovery", &LifeSupportDataComponent::IssWaterRecovery)
			.Field("OxygenGasConstant", &LifeSupportDataComponent::OxygenGasConstant)
			.Field("OxygenHeatCapacityRatio", &LifeSupportDataComponent::OxygenHeatCapacityRatio);

		registry.Register<LifeSupportConfigComponent>("LifeSupportConfig")
			.Field("CrewAboard", &LifeSupportConfigComponent::CrewAboard)
			.Field("CabOxygenReserveKg", &LifeSupportConfigComponent::CabOxygenReserveKg)
			.Field("CabWaterReserveKg", &LifeSupportConfigComponent::CabWaterReserveKg)
			.Field("WaterRecovery", &LifeSupportConfigComponent::WaterRecovery)
			.Field("ScrubberCapacity", &LifeSupportConfigComponent::ScrubberCapacity)
			.Field("PowerDrawPerSecond", &LifeSupportConfigComponent::PowerDrawPerSecond)
			.Field("PowerGenerationPerSecond", &LifeSupportConfigComponent::PowerGenerationPerSecond)
			.Field("BoostOxygenKgPerSecond", &LifeSupportConfigComponent::BoostOxygenKgPerSecond)
			.Field("LeakBoostPenalty", &LifeSupportConfigComponent::LeakBoostPenalty);

		registry.Register<TankConfigComponent>("TankConfig")
			.Field("OxygenTankKg", &TankConfigComponent::OxygenTankKg)
			.Field("WaterTankKg", &TankConfigComponent::WaterTankKg)
			.Field("TankVolumeM3", &TankConfigComponent::TankVolumeM3)
			.Field("TankTemperatureK", &TankConfigComponent::TankTemperatureK)
			.Field("HoleAreaM2", &TankConfigComponent::HoleAreaM2)
			.Field("DischargeCoefficient", &TankConfigComponent::DischargeCoefficient)
			.Field("WaterLeakKgPerSecond", &TankConfigComponent::WaterLeakKgPerSecond)
			.Field("Spacing", &TankConfigComponent::Spacing)
			.Field("PerRow", &TankConfigComponent::PerRow);

		registry.Register<CrewMemberComponent>("CrewMember");

		registry.Register<DepotComponent>("Depot")
			.Field("StartSpacing", &DepotComponent::StartSpacing);

		registry.Register<ColonyComponent>("Colony")
			.Field("ArrivalRadius", &ColonyComponent::ArrivalRadius);

		static const Array<const char*> interiorKinds = { "Bay", "Cab" };
		registry.Register<InteriorComponent>("Interior")
			.Field("Crew", &InteriorComponent::Crew)
			.Field("Kind", &InteriorComponent::Kind).Choices(interiorKinds)
			.Field("HalfWidth", &InteriorComponent::HalfWidth)
			.Field("HalfLength", &InteriorComponent::HalfLength);

		registry.Register<AirlockConfigComponent>("AirlockConfig")
			.Field("OpenSeconds", &AirlockConfigComponent::OpenSeconds)
			.Field("PullSpeed", &AirlockConfigComponent::PullSpeed)
			.Field("FlushRadius", &AirlockConfigComponent::FlushRadius)
			.Field("VentOxygenKg", &AirlockConfigComponent::VentOxygenKg)
			.Field("LeakPenaltyKg", &AirlockConfigComponent::LeakPenaltyKg)
			.Field("CooldownSeconds", &AirlockConfigComponent::CooldownSeconds);

		registry.Register<AirlockComponent>("Airlock")
			.Field("Crew", &AirlockComponent::Crew);

		registry.Register<BayHatchComponent>("BayHatch")
			.Field("Crew", &BayHatchComponent::Crew);

		registry.Register<OnFootConfigComponent>("OnFootConfig")
			.Field("MoveSpeed", &OnFootConfigComponent::MoveSpeed)
			.Field("LookSensitivity", &OnFootConfigComponent::LookSensitivity)
			.Field("EyeHeight", &OnFootConfigComponent::EyeHeight)
			.Field("BodyRadius", &OnFootConfigComponent::BodyRadius)
			.Field("LaserRange", &OnFootConfigComponent::LaserRange)
			.Field("FireInterval", &OnFootConfigComponent::FireInterval)
			.Field("LaserDamage", &OnFootConfigComponent::LaserDamage)
			.Field("BeamSeconds", &OnFootConfigComponent::BeamSeconds)
			.Field("PatchSecondsPerHole", &OnFootConfigComponent::PatchSecondsPerHole);

		registry.Register<SiphonConfigComponent>("SiphonConfig")
			.Field("MitePoolPerCrew", &SiphonConfigComponent::MitePoolPerCrew)
			.Field("MiteHealth", &SiphonConfigComponent::MiteHealth)
			.Field("MiteSpeed", &SiphonConfigComponent::MiteSpeed)
			.Field("MiteDrainKgPerSecond", &SiphonConfigComponent::MiteDrainKgPerSecond)
			.Field("MiteBellyKg", &SiphonConfigComponent::MiteBellyKg)
			.Field("MiteBiteHoleSeconds", &SiphonConfigComponent::MiteBiteHoleSeconds)
			.Field("MiteTrickleSeconds", &SiphonConfigComponent::MiteTrickleSeconds);

		const char* wav = "Audio (*.wav)";
		registry.Register<SoundCuesComponent>("SoundCues")
			.Field("LaserFire", &SoundCuesComponent::LaserFire).AssetPath(wav)
			.Field("RepairBeam", &SoundCuesComponent::RepairBeam).AssetPath(wav)
			.Field("TankPuncture", &SoundCuesComponent::TankPuncture).AssetPath(wav)
			.Field("TankPatched", &SoundCuesComponent::TankPatched).AssetPath(wav)
			.Field("MiteSpawn", &SoundCuesComponent::MiteSpawn).AssetPath(wav)
			.Field("MiteChitter", &SoundCuesComponent::MiteChitter).AssetPath(wav)
			.Field("MiteBite", &SoundCuesComponent::MiteBite).AssetPath(wav)
			.Field("MiteDeath", &SoundCuesComponent::MiteDeath).AssetPath(wav)
			.Field("Boost", &SoundCuesComponent::Boost).AssetPath(wav)
			.Field("Hatch", &SoundCuesComponent::Hatch).AssetPath(wav)
			.Field("AirlockVent", &SoundCuesComponent::AirlockVent).AssetPath(wav)
			.Field("Countdown", &SoundCuesComponent::Countdown).AssetPath(wav)
			.Field("Go", &SoundCuesComponent::Go).AssetPath(wav);
	}

	void AddSystems(World& world)
	{
		world.AddSystem<SoundPreloadSystem>();
		// Seats first so every later system sees this frame's crews and roles.
		world.AddSystem<CrewSystem>();
		world.AddSystem<MatchSystem>();
		world.AddSystem<ColonyClockSystem>();
		world.AddSystem<LifeSupportSystem>();
		world.AddSystem<CargoTankSystem>();
		if (DefenderEnabled)
		{
			world.AddSystem<SiphonSpawnerSystem>();
			world.AddSystem<SiphonSystem>();
			world.AddSystem<AirlockSystem>();
		}
		world.AddSystem<OnFootSystem>();
		// Starts the Driver's own systems at GO for a seated Driver, and the hatch between cab and bay.
		world.AddSystem<DriverLauncherSystem>();
		world.AddSystem<LifeSupportReadoutSystem>();
	}
}
