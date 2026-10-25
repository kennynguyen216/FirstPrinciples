#include "Crew/BayActions.h"
#include "Audio/Sounds.h"
#include "Crew/CrewComponents.h"
#include "Enemies/SiphonSystems.h"
#include "LifeSupport/Cargo.h"

using namespace FP;

namespace BayActions
{
	void HitSiphon(World& world, Entity entity, float damage)
	{
		SiphonComponent* siphon = world.TryGet<SiphonComponent>(entity);
		if (!siphon || !siphon->Active)
			return;
		siphon->Health -= damage;
		++siphon->Hits;
		if (siphon->Health <= 0)
			Siphons::Kill(world, entity);
	}

	void PunctureTank(World& world, Entity entity, Vec3 where)
	{
		CargoTankComponent* tank = world.TryGet<CargoTankComponent>(entity);
		if (!tank)
			return;
		++tank->Holes;
		tank->LeakPoint = where;
		if (TruckResourcesComponent* resources = Cargo::FindResources(world, tank->Crew))
			++resources->TanksShot;
		FP_LOG("Laser: shot punctured %s", world.GetEntityName(entity).CStr());
		Sounds::PlayAt(world, SoundCue::TankPuncture, where);
	}

	void RepairTank(World& world, Entity entity, float seconds, Vec3 where)
	{
		CargoTankComponent* tank = world.TryGet<CargoTankComponent>(entity);
		if (!tank || tank->Holes <= 0)
			return;
		float patchSeconds = 1.5f;
		for (auto [configEntity, config] : world.View<OnFootConfigComponent>().each())
			patchSeconds = config.PatchSecondsPerHole;
		tank->PatchProgress += seconds;
		if (tank->PatchProgress < patchSeconds)
			return;
		tank->PatchProgress = 0;
		--tank->Holes;
		if (TruckResourcesComponent* resources = Cargo::FindResources(world, tank->Crew))
			++resources->HolesPatched;
		Sounds::PlayAt(world, SoundCue::TankPatched, where);
		if (tank->Holes == 0)
			FP_LOG("Laser: patched %s", world.GetEntityName(entity).CStr());
	}

	Entity FindTank(World& world, int32 crew, int32 index)
	{
		for (auto [entity, tank] : world.View<CargoTankComponent>().each())
		{
			if (tank.Crew == crew && tank.Index == index)
				return entity;
		}
		return NullEntity;
	}

	Entity FindSiphon(World& world, int32 crew, int32 index)
	{
		for (auto [entity, siphon] : world.View<SiphonComponent>().each())
		{
			if (siphon.Crew == crew && siphon.Index == index)
				return entity;
		}
		return NullEntity;
	}
}
