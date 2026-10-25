#include "Enemies/Airlock.h"
#include "Audio/Sounds.h"
#include "Enemies/SiphonSystems.h"
#include "LifeSupport/Cargo.h"
#include "Match/MatchComponents.h"
#include "Net/GameNet.h"

#include <algorithm>

using namespace FP;

namespace
{
	const char* const ClosedMaterial = "Materials/Airlock.material";
	const char* const OpenMaterial = "Materials/AirlockOpen.material";

	AirlockConfigComponent GetConfig(World& world)
	{
		for (auto [entity, config] : world.View<AirlockConfigComponent>().each())
			return config;
		return {};
	}

	AirlockComponent* FindAirlock(World& world, int32 crew)
	{
		for (auto [entity, airlock] : world.View<AirlockComponent>().each())
		{
			if (airlock.Crew == crew)
				return &airlock;
		}
		return nullptr;
	}
}

namespace Airlock
{
	bool Vent(World& world, int32 crew)
	{
		AirlockComponent* airlock = FindAirlock(world, crew);
		if (!airlock || airlock->CooldownFor > 0)
			return false;
		const AirlockConfigComponent config = GetConfig(world);
		airlock->OpenFor = config.OpenSeconds;
		airlock->CooldownFor = config.CooldownSeconds;

		const float vented = Cargo::Draw(world, crew, CargoKind::Oxygen, config.VentOxygenKg + config.LeakPenaltyKg * Cargo::Sum(world, crew).LeakingTanks);
		if (TruckResourcesComponent* resources = Cargo::FindResources(world, crew))
			resources->OxygenVentedKg += vented;
		FP_LOG("Airlock: Crew %s vented the cargo bay (%.1f kg of oxygen)", GetCrewName(crew), vented);
		Sounds::Play(world, SoundCue::AirlockVent);
		return true;
	}

	bool IsOpen(World& world, int32 crew)
	{
		const AirlockComponent* airlock = FindAirlock(world, crew);
		return airlock && airlock->OpenFor > 0;
	}

	bool IsSealed(World& world, int32 crew)
	{
		for (auto [entity, door] : world.View<BlastDoorComponent>().each())
		{
			if (door.Crew == crew && door.Shut > 0)
				return true;
		}
		return false;
	}
}

void AirlockSystem::OnStart(World& world)
{
	for (auto [entity, airlock] : world.View<AirlockComponent>().each())
		airlock.OpenFor = airlock.CooldownFor = 0;
	for (auto [entity, transform, door] : world.View<TransformComponent, BlastDoorComponent>().each())
	{
		door.Raised = transform.Position;
		door.Shut = 0;
	}
}

void AirlockSystem::Update(World& world, float deltaTime)
{
	const AirlockConfigComponent config = GetConfig(world);
	// Every machine slides its doors from the airlocks it knows about, which the host keeps in sync.
	for (auto [entity, transform, door] : world.View<TransformComponent, BlastDoorComponent>().each())
	{
		const bool close = Airlock::IsOpen(world, door.Crew);
		const float seconds = std::max(close ? door.CloseSeconds : door.OpenSeconds, 0.01f);
		door.Shut = std::clamp(door.Shut + (close ? 1 : -1) * deltaTime / seconds, 0.0f, 1.0f);
		transform.Position = door.Raised - Vec3{ 0, door.Drop * door.Shut, 0 };
	}

	for (auto [airlockEntity, door, airlock, material] : world.View<TransformComponent, AirlockComponent, MaterialComponent>().each())
	{
		airlock.CooldownFor = std::max(0.0f, airlock.CooldownFor - deltaTime);
		const bool open = airlock.OpenFor > 0;
		material.Material = open ? OpenMaterial : ClosedMaterial;
		if (!open)
			continue;
		airlock.OpenFor -= deltaTime;
		if (!GameNet::IsHost(world))
			continue;

		for (auto [entity, transform, siphon] : world.View<TransformComponent, SiphonComponent>().each())
		{
			if (!siphon.Active || siphon.Crew != airlock.Crew)
				continue;
			const Vec3 toDoor = door.Position - transform.Position;
			const float distance = toDoor.Length();
			if (distance <= config.FlushRadius)
			{
				Siphons::Flush(world, entity);
				continue;
			}
			transform.Position += toDoor * (std::min(distance, config.PullSpeed * deltaTime) / distance);
		}
	}
}
