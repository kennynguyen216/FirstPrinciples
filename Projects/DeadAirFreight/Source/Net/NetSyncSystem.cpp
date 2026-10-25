#include "Net/NetSyncSystem.h"
#include "Crew/BayActions.h"
#include "Enemies/Airlock.h"
#include "Enemies/SiphonComponents.h"
#include "LifeSupport/LifeSupportComponents.h"
#include "Match/MatchQueries.h"
#include "Net/DriverNet.h"
#include "Net/GameNet.h"

#include <algorithm>
#include <cstring>

using namespace FP;
using namespace MatchQueries;
using GameNet::Message;

namespace
{
	constexpr float MatchStateInterval = 0.2f;
	constexpr float BayStateInterval = 0.05f;

	DriverNetComponent* GetLink(World& world)
	{
		const Entity match = FindMatch(world);
		return match == NullEntity ? nullptr : world.TryGet<DriverNetComponent>(match);
	}

	void ApplyBayState(World& world, NetReader& in)
	{
		const int32 crew = in.ReadU8();
		const int32 tanks = in.ReadU8();
		for (int32 i = 0; i < tanks; ++i)
		{
			const float amount = in.ReadFloat();
			const int32 holes = in.ReadU8();
			const float patch = in.ReadFloat();
			// Only a holed tank sends where it leaks from.
			const Vec3 leak = holes > 0 ? in.ReadVec3() : Vec3{};
			if (!in.IsValid())
				return;
			if (CargoTankComponent* tank = world.TryGet<CargoTankComponent>(BayActions::FindTank(world, crew, i)))
			{
				tank->AmountKg = amount;
				tank->Holes = holes;
				tank->PatchProgress = patch;
				if (holes > 0)
					tank->LeakPoint = leak;
			}
		}
		const int32 siphons = in.ReadU8();
		for (int32 i = 0; i < siphons; ++i)
		{
			const bool active = in.ReadBool();
			const Vec3 position = in.ReadVec3();
			const float size = in.ReadFloat();
			const uint8 hits = in.ReadU8();
			const uint8 pops = in.ReadU8();
			if (!in.IsValid())
				return;
			const Entity entity = BayActions::FindSiphon(world, crew, i);
			if (entity == NullEntity)
				continue;
			SiphonComponent& siphon = world.Get<SiphonComponent>(entity);
			siphon.Active = active;
			siphon.Size = size;
			siphon.Hits = hits;
			siphon.Pops = pops;
			world.Get<TransformComponent>(entity).Position = position;
		}
		const float airlockOpen = in.ReadFloat();
		for (auto [entity, airlock] : world.View<AirlockComponent>().each())
		{
			if (airlock.Crew == crew && in.IsValid())
				airlock.OpenFor = airlockOpen;
		}
	}
}

void NetSyncSystem::OnStart(World& world)
{
	m_SinceMatchState = m_SinceBayState = 0;
	GameNet::SetHostId(0);
	if (const Entity match = FindMatch(world); match != NullEntity)
		world.Add<DriverNetComponent>(match);
}

void NetSyncSystem::Update(World& world, float deltaTime)
{
	if (!GameNet::IsNetworked(world))
		return;

	NetEvent event;
	while (GameNet::Poll(world, event))
		Handle(world, event);

	if (!GameNet::IsHost(world))
		return;
	m_SinceMatchState += deltaTime;
	m_SinceBayState += deltaTime;
	if (m_SinceMatchState >= MatchStateInterval)
	{
		m_SinceMatchState = 0;
		GameNet::Send(world, GameNet::Begin(Message::HostClaim), NetChannel::Sequenced);
		SendMatchState(world);
	}
	if (m_SinceBayState >= BayStateInterval)
	{
		m_SinceBayState = 0;
		for (int32 crew = 0; crew < CrewCount; ++crew)
			SendBayState(world, crew);
	}
}

void NetSyncSystem::Handle(World& world, const NetEvent& event)
{
	NetReader in(event.Data.Data(), event.Data.Num());
	const Message type = static_cast<Message>(in.ReadU8());
	const bool host = GameNet::IsHost(world);
	DriverNetComponent* link = GetLink(world);

	switch (type)
	{
		case Message::HostClaim:
			if (!host)
				GameNet::SetHostId(event.Peer);
			break;
		case Message::MatchState:
		{
			MatchStateComponent* state = GetState(world);
			const MatchPhase phase = static_cast<MatchPhase>(in.ReadU8());
			const float countdown = in.ReadFloat();
			const float raceTime = in.ReadFloat();
			const int32 winner = static_cast<int32>(in.ReadU8()) - 1;
			if (host || !state || !in.IsValid())
				return;
			state->Phase = phase;
			state->Countdown = countdown;
			state->RaceTime = raceTime;
			state->WinnerCrew = winner;
			break;
		}
		case Message::DriverPackage:
		{
			const int32 piece = in.ReadU16();
			const int32 total = in.ReadU16();
			const String text = in.ReadString();
			if (host || !link || !in.IsValid() || link->PackageReady || piece != link->PackagePieces)
				return;
			link->PackageText += text.CStr();
			link->PackageTotal = total;
			if (++link->PackagePieces == total)
				link->PackageReady = FromText(link->PackageText, link->Package);
			break;
		}
		case Message::TruckState:
		{
			if (host || !link || in.Remaining() < static_cast<int32>(sizeof(TruckState) + sizeof(LifeSupport) + 8))
				return;
			std::memcpy(&link->Truck, in.ReadBytes(sizeof(TruckState)), sizeof(TruckState));
			std::memcpy(&link->Life, in.ReadBytes(sizeof(LifeSupport)), sizeof(LifeSupport));
			link->WheelHolder = in.ReadU64();
			link->HasTruck = true;
			break;
		}
		case Message::BayState:
			if (!host)
				ApplyBayState(world, in);
			break;
		case Message::DriverResults:
		{
			const String reason = in.ReadString();
			if (!host && link && in.IsValid())
				link->Results = reason.CStr();
			break;
		}
		case Message::CrewInput:
		{
			const bool seated = in.ReadBool();
			const bool repairing = in.ReadBool();
			const float aimX = in.ReadFloat();
			const float aimY = in.ReadFloat();
			if (!host || !link || !in.IsValid())
				return;
			DriverNetComponent::RemoteCrew& crew = link->GetRemote(event.Peer);
			crew.Seated = seated;
			crew.Repairing = repairing;
			crew.Controls.Aim = { aimX, aimY };
			crew.LastHeard = world.GetTime();
			break;
		}
		case Message::CrewInCab:
		{
			const bool inCab = in.ReadBool();
			const Vec3 position = in.ReadVec3();
			const float yaw = in.ReadFloat();
			if (!link || !in.IsValid())
				return;
			DriverNetComponent::CabCrewmate& crewmate = link->GetCabCrewmate(event.Peer);
			crewmate.InCab = inCab;
			crewmate.CabPosition = position;
			crewmate.Yaw = yaw;
			crewmate.LastHeard = world.GetTime();
			break;
		}
		case Message::LaserHit:
		{
			const bool siphon = in.ReadBool();
			const int32 crew = in.ReadU8();
			const int32 index = in.ReadU8();
			const float damage = in.ReadFloat();
			const Vec3 where = in.ReadVec3();
			if (!host || !in.IsValid())
				return;
			if (siphon)
				BayActions::HitSiphon(world, BayActions::FindSiphon(world, crew, index), damage);
			else
				BayActions::PunctureTank(world, BayActions::FindTank(world, crew, index), where);
			break;
		}
		case Message::LaserRepair:
		{
			const int32 crew = in.ReadU8();
			const int32 index = in.ReadU8();
			const float seconds = in.ReadFloat();
			const Vec3 where = in.ReadVec3();
			if (host && in.IsValid())
				BayActions::RepairTank(world, BayActions::FindTank(world, crew, index), seconds, where);
			break;
		}
		case Message::Vent:
		{
			const int32 crew = in.ReadU8();
			if (host && in.IsValid())
				Airlock::Vent(world, crew);
			break;
		}
	}
}

void NetSyncSystem::SendMatchState(World& world)
{
	const MatchStateComponent* state = GetState(world);
	if (!state)
		return;
	NetWriter message = GameNet::Begin(Message::MatchState);
	message.WriteU8(static_cast<uint8>(state->Phase));
	message.WriteFloat(state->Countdown);
	message.WriteFloat(state->RaceTime);
	message.WriteU8(static_cast<uint8>(state->WinnerCrew + 1));
	GameNet::Send(world, message, NetChannel::Sequenced);
}

void NetSyncSystem::SendBayState(World& world, int32 crew)
{
	NetWriter message = GameNet::Begin(Message::BayState);
	message.WriteU8(static_cast<uint8>(crew));

	int32 tanks = 0;
	for (auto [entity, tank] : world.View<CargoTankComponent>().each())
		tanks += tank.Crew == crew ? 1 : 0;
	message.WriteU8(static_cast<uint8>(tanks));
	for (int32 i = 0; i < tanks; ++i)
	{
		const CargoTankComponent* tank = world.TryGet<CargoTankComponent>(BayActions::FindTank(world, crew, i));
		message.WriteFloat(tank ? tank->AmountKg : 0);
		const uint8 holes = tank ? static_cast<uint8>(std::clamp(tank->Holes, 0, 255)) : 0;
		message.WriteU8(holes);
		message.WriteFloat(tank ? tank->PatchProgress : 0);
		if (holes > 0)
			message.WriteVec3(tank->LeakPoint);
	}

	int32 siphons = 0;
	for (auto [entity, siphon] : world.View<SiphonComponent>().each())
		siphons += siphon.Crew == crew ? 1 : 0;
	message.WriteU8(static_cast<uint8>(siphons));
	for (int32 i = 0; i < siphons; ++i)
	{
		const Entity entity = BayActions::FindSiphon(world, crew, i);
		const SiphonComponent* siphon = entity == NullEntity ? nullptr : world.TryGet<SiphonComponent>(entity);
		const TransformComponent* transform = entity == NullEntity ? nullptr : world.TryGet<TransformComponent>(entity);
		message.WriteBool(siphon && siphon->Active);
		message.WriteVec3(transform ? transform->Position : Vec3{});
		message.WriteFloat(siphon ? siphon->Size : 0);
		message.WriteU8(siphon ? siphon->Hits : 0);
		message.WriteU8(siphon ? siphon->Pops : 0);
	}

	float airlockOpen = 0;
	for (auto [entity, airlock] : world.View<AirlockComponent>().each())
		airlockOpen = airlock.Crew == crew ? airlock.OpenFor : airlockOpen;
	message.WriteFloat(airlockOpen);
	GameNet::Send(world, message, NetChannel::Sequenced);
}
