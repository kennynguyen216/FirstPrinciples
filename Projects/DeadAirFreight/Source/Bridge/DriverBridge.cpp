#include "Bridge/DriverBridge.h"
#include "Driver/DriverSystem.h"
#include "LifeSupport/Cargo.h"
#include "Match/MatchQueries.h"

using namespace FP;
using namespace MatchQueries;

namespace
{
	// The Driver scene names its truck and colony; the Driver's systems find them the same way.
	constexpr const char* TruckName = "Truck";
	constexpr const char* ColonyName = "Colony";

	// What the tanks held when the Driver's systems started this frame.
	struct CargoBridgeComponent
	{
		float Before = 0;
	};

	Entity FindByName(World& world, const char* name)
	{
		const Entity entity = world.FindEntityByName(name);
		return world.IsValid(entity) ? entity : NullEntity;
	}
}

void DriverLauncherSystem::OnStart(World&)
{
	m_Launched = false;
}

void DriverLauncherSystem::Update(World& world, float)
{
	const MatchStateComponent* state = GetState(world);
	if (m_Launched || !state || state->Phase != MatchPhase::Racing)
		return;
	m_Launched = true;

	const Entity local = FindLocalMember(world);
	if (local == NullEntity)
		return;
	const CrewMemberComponent& member = world.Get<CrewMemberComponent>(local);
	if (member.Slot < 0 || member.Role != CrewRole::Driver)
		return;
	if (FindByName(world, TruckName) == NullEntity)
	{
		FP_LOG_WARN("Driver bridge: the scene has no entity named Truck, so there is nothing to drive");
		return;
	}

	if (TruckResourcesComponent* resources = Cargo::FindResources(world, member.Crew))
		resources->DriverCab = true;
	// Added in this order so the bridge runs right before and right after the Driver's systems.
	world.AddSystem<CargoBridgeInSystem>(member.Crew);
	world.AddSystem<DriverSystem>();
	world.AddSystem<CargoBridgeOutSystem>(member.Crew);
	FP_LOG("Driver bridge: Crew %s's Driver has the wheel", GetCrewName(member.Crew));
}

void CargoBridgeInSystem::Update(World& world, float)
{
	const Entity truck = FindByName(world, TruckName);
	LifeSupport* life = truck == NullEntity ? nullptr : world.TryGet<LifeSupport>(truck);
	if (!life)
		return;
	life->Cargo = Cargo::Sum(world, m_Crew).OxygenKg;
	world.Add<CargoBridgeComponent>(truck).Before = life->Cargo;
}

void CargoBridgeOutSystem::OnStart(World&)
{
	DriverSettings settings;
	if (LoadDriverSettings(Paths::Content("Settings/Driver.settings").CStr(), settings))
	{
		m_DeliveryAltitude = settings.DeliveryAltitude;
		m_ColonyRadius = settings.ColonyRadius;
	}
}

void CargoBridgeOutSystem::Update(World& world, float)
{
	const Entity truck = FindByName(world, TruckName);
	LifeSupport* life = truck == NullEntity ? nullptr : world.TryGet<LifeSupport>(truck);
	const CargoBridgeComponent* bridge = truck == NullEntity ? nullptr : world.TryGet<CargoBridgeComponent>(truck);
	if (!life || !bridge)
		return;

	// Spent comes out of the tanks; won back goes into them.
	const float spent = bridge->Before - life->Cargo;
	if (spent > 0)
		Cargo::Draw(world, m_Crew, CargoKind::Oxygen, spent);
	else if (spent < 0)
		Cargo::Refill(world, m_Crew, CargoKind::Oxygen, -spent);

	// The networked crew entity follows the truck, so other peers can see it once trucks are shared.
	const TransformComponent& truckTransform = world.Get<TransformComponent>(truck);
	if (const Entity local = FindLocalMember(world); local != NullEntity)
	{
		TransformComponent& crewTransform = world.Get<TransformComponent>(local);
		crewTransform.Position = truckTransform.Position;
		crewTransform.Rotation = truckTransform.Rotation;
	}

	// The Driver's match ends on the same three conditions the Driver's systems use.
	MatchStateComponent* state = GetState(world);
	if (!state || state->Phase != MatchPhase::Racing)
		return;
	const Entity colony = FindByName(world, ColonyName);
	const bool delivered = colony != NullEntity &&
		(truckTransform.Position - world.Get<TransformComponent>(colony).Position).Length() - m_ColonyRadius <= m_DeliveryAltitude;
	if (!delivered && life->ColonyTimeLeft > 0 && life->Cargo > 0)
		return;
	state->Phase = MatchPhase::Results;
	state->WinnerCrew = delivered ? m_Crew : -1;
	if (delivered)
	{
		state->Crews[m_Crew].Arrived = true;
		state->Crews[m_Crew].ArrivalTime = state->RaceTime;
		state->Crews[m_Crew].OxygenDeliveredKg = life->Cargo;
	}
	FP_LOG("Match: over. %s", delivered ? "Delivered" : life->ColonyTimeLeft <= 0 ? "The colony ran out of air" : "No cargo left");
}
