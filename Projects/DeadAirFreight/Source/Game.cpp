#include "Game.h"
#include "LifeSupport/LifeSupportSystems.h"
#include "Match/MatchSystems.h"
#include "Physics/PhysicsComponents.h"
#include "Truck/TruckSystems.h"

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

		registry.Register<ColliderComponent>("Collider")
			.Field("Shape", &ColliderComponent::Shape).Choices(GetColliderShapeNames())
			.Field("HalfExtents", &ColliderComponent::HalfExtents)
			.Field("Radius", &ColliderComponent::Radius)
			.Field("HalfHeight", &ColliderComponent::HalfHeight)
			.Field("Layer", &ColliderComponent::Layer).Choices(GetPhysicsLayerNames());

		registry.Register<RigidBodyComponent>("RigidBody")
			.Field("Motion", &RigidBodyComponent::Motion).Choices(GetMotionTypeNames())
			.Field("Mass", &RigidBodyComponent::Mass)
			.Field("LinearDamping", &RigidBodyComponent::LinearDamping)
			.Field("AngularDamping", &RigidBodyComponent::AngularDamping)
			.Field("GravityFactor", &RigidBodyComponent::GravityFactor);

		registry.Register<TruckConfigComponent>("TruckConfig")
			.Field("Thrust", &TruckConfigComponent::Thrust)
			.Field("MaxSpeed", &TruckConfigComponent::MaxSpeed)
			.Field("BoostMultiplier", &TruckConfigComponent::BoostMultiplier)
			.Field("TurnRate", &TruckConfigComponent::TurnRate)
			.Field("Drag", &TruckConfigComponent::Drag)
			.Field("Size", &TruckConfigComponent::Size);

		registry.Register<CrewMemberComponent>("CrewMember");

		registry.Register<DepotComponent>("Depot")
			.Field("StartSpacing", &DepotComponent::StartSpacing);

		registry.Register<ColonyComponent>("Colony")
			.Field("ArrivalRadius", &ColonyComponent::ArrivalRadius);

		registry.Register<CargoBayComponent>("CargoBay")
			.Field("Crew", &CargoBayComponent::Crew);

		registry.Register<ChaseCameraComponent>("ChaseCamera")
			.Field("Distance", &ChaseCameraComponent::Distance)
			.Field("Height", &ChaseCameraComponent::Height)
			.Field("Smoothing", &ChaseCameraComponent::Smoothing);
	}

	void AddSystems(World& world)
	{
		// Seats first so every later system sees this frame's crews and roles.
		world.AddSystem<CrewSystem>();
		world.AddSystem<MatchSystem>();
		world.AddSystem<ColonyClockSystem>();
		world.AddSystem<LifeSupportSystem>();
		world.AddSystem<CargoTankSystem>();
		world.AddSystem<TruckSystem>();
		world.AddSystem<ChaseCameraSystem>();
		world.AddSystem<LifeSupportReadoutSystem>();
	}
}
