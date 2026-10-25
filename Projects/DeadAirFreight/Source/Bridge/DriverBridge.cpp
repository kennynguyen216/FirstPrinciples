#include "Bridge/DriverBridge.h"
#include "Crew/CrewComponents.h"
#include "Crew/TransitSystem.h"
#include "Effects/Starfield.h"
#include "Driver/Cab.h"
#include "Driver/DriverSystem.h"
#include "Enemies/SiphonComponents.h"
#include "LifeSupport/Cargo.h"
#include "Match/MatchQueries.h"
#include "Net/DriverNet.h"
#include "Net/GameNet.h"
#include "Title/TitleComponents.h"

#include "Engine/UI/UISystem.h"

#include <algorithm>
#include <cstdio>

using namespace FP;
using namespace MatchQueries;

namespace
{
	// The Driver scene names its truck, colony, seat and the player's body in the cab; the Driver's
	// systems find them the same way.
	constexpr const char* TruckName = "Truck";
	constexpr const char* ColonyName = "Colony";
	constexpr const char* CabBodyName = "Driver";
	constexpr const char* SeatName = "CabSeat";
	constexpr const char* CabFloorName = "CabFloor";
	// Pieces of the match package, in characters, small enough for one network message.
	constexpr int32 PackagePiece = 800;
	// A crewmate on another machine not heard from for this long has left the wheel, or the cab.
	constexpr double RemoteTimeout = 1.0;
	// Hidden stand-ins shrink to almost nothing; the engine refuses entities scaled to zero.
	const Vec3 HiddenScale{ 0.001f, 0.001f, 0.001f };

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

	DriverNetComponent* GetLink(World& world)
	{
		const Entity match = FindMatch(world);
		return match == NullEntity ? nullptr : world.TryGet<DriverNetComponent>(match);
	}

	DriverState* GetCab(World& world)
	{
		const Entity body = FindByName(world, CabBodyName);
		return body == NullEntity ? nullptr : world.TryGet<DriverState>(body);
	}

	void SendPackage(World& world, const MatchPackage& package)
	{
		const std::string text = ToText(package);
		const int32 total = static_cast<int32>((text.size() + PackagePiece - 1) / PackagePiece);
		for (int32 piece = 0; piece < total; ++piece)
		{
			NetWriter message = GameNet::Begin(GameNet::Message::DriverPackage);
			message.WriteU16(static_cast<uint16>(piece));
			message.WriteU16(static_cast<uint16>(total));
			message.WriteString(std::string_view(text).substr(piece * PackagePiece, PackagePiece));
			GameNet::Send(world, message, NetChannel::Reliable);
		}
	}

	// Tells everyone where this player stands in the cab, in cab space: where they walk, or the seat.
	void SendCabSpot(World& world, Vec3 seat)
	{
		const DriverState* cab = GetCab(world);
		const Entity local = FindLocalMember(world);
		const bool inCab = cab && cab->InCab && local != NullEntity && !world.Has<OnFootComponent>(local);
		NetWriter message = GameNet::Begin(GameNet::Message::CrewInCab);
		message.WriteBool(inCab);
		message.WriteVec3(cab && cab->Seated ? Vec3{ seat.X, cab->Position.Y, seat.Z } : cab ? cab->Position : Vec3{});
		message.WriteFloat(cab && !cab->Seated ? cab->Yaw : 0);
		GameNet::Send(world, message, NetChannel::Sequenced);
	}

	// Stands each crewmate who is in the cab inside this machine's copy of it.
	void PlaceCabCrewmates(World& world, DriverNetComponent& link, const TransformComponent& truck, Vec3 cabOrigin)
	{
		const Quat rotation = truck.GetRotation();
		for (DriverNetComponent::CabCrewmate& crewmate : link.CabCrew)
		{
			if (crewmate.StandIn == NullEntity || !world.IsValid(crewmate.StandIn))
			{
				crewmate.StandIn = world.CreateEntity("Crewmate In Cab");
				world.Add<MeshComponent>(crewmate.StandIn).Mesh = "Cube";
				world.Add<MaterialComponent>(crewmate.StandIn).Material = "Materials/Defender.material";
			}
			TransformComponent& body = world.Get<TransformComponent>(crewmate.StandIn);
			if (!crewmate.InCab || world.GetTime() - crewmate.LastHeard > RemoteTimeout)
			{
				body.Scale = HiddenScale;
				continue;
			}
			const Vec3 middle = crewmate.CabPosition + Vec3{ 0, WalkerSize.Y * 0.5f, 0 };
			body.Position = truck.Position + rotation.Rotate(middle - cabOrigin);
			body.Rotation = (rotation * Quat::FromEuler({ 0, crewmate.Yaw, 0 })).ToEuler();
			body.Scale = WalkerSize;
		}
	}

	// The bay from above for the HUD: its tanks, every Mite in it and this player when they walk
	// there, so whoever is up in the cab can tell something is feeding in the back. Spots are -1 to 1
	// across (x, the airlock wall is -1) and along (z, the hatch is +1). Built from what every machine
	// syncs, so a Mite counts as feeding when it perches on an oxygen tank's rim.
	void SendBayHud(World& world, int32 crew)
	{
		UIDocumentInstance* page = nullptr;
		for (auto [entity, camera, ui] : world.View<CameraComponent, UIComponent>().each())
			page = camera.Primary ? UISystem::For(world, entity) : page;
		Vec3 bay;
		InteriorComponent info;
		if (!page || !page->IsReady() || FindInterior(world, crew, "Bay", &bay, &info) == NullEntity)
			return;
		std::string json;
		auto append = [&json](const char* format, auto... args) {
			char text[128];
			std::snprintf(text, sizeof(text), format, args...);
			json += text;
		};
		auto spotX = [&](Vec3 at) { return std::clamp((at.X - bay.X) / info.HalfWidth, -1.0f, 1.0f); };
		auto spotZ = [&](Vec3 at) { return std::clamp((at.Z - bay.Z) / info.HalfLength, -1.0f, 1.0f); };

		json = "{\"tanks\":[";
		for (auto [entity, transform, tank] : world.View<TransformComponent, CargoTankComponent>().each())
		{
			if (tank.Crew == crew)
				append("%s[%.3f,%.3f,%.2f,%d,%d]", json.back() == '[' ? "" : ",", spotX(transform.Position), spotZ(transform.Position),
					tank.CapacityKg > 0 ? tank.AmountKg / tank.CapacityKg : 0.0f, tank.Holes, tank.Kind == CargoKind::Water ? 1 : 0);
		}
		json += "],\"mites\":[";
		int32 mites = 0;
		int32 feeding = 0;
		for (auto [entity, transform, siphon] : world.View<TransformComponent, SiphonComponent>().each())
		{
			if (!siphon.Active || siphon.Crew != crew)
				continue;
			bool eating = false;
			for (auto [tankEntity, tankTransform, tank] : world.View<TransformComponent, CargoTankComponent>().each())
			{
				const Vec3 apart = transform.Position - tankTransform.Position;
				const float reach = tankTransform.Scale.X * 0.5f + 0.4f;
				eating |= tank.Crew == crew && tank.Kind == CargoKind::Oxygen && tank.AmountKg > 0 && apart.X * apart.X + apart.Z * apart.Z < reach * reach;
			}
			append("%s[%.3f,%.3f,%d]", mites ? "," : "", spotX(transform.Position), spotZ(transform.Position), eating ? 1 : 0);
			++mites;
			feeding += eating ? 1 : 0;
		}
		json += "]";
		const Entity local = FindLocalMember(world);
		const OnFootComponent* onFoot = local == NullEntity ? nullptr : world.TryGet<OnFootComponent>(local);
		if (onFoot && onFoot->Crew == crew && onFoot->Interior == "Bay")
		{
			const Vec3 at = world.Get<TransformComponent>(local).Position;
			append(",\"you\":[%.3f,%.3f]", spotX(at), spotZ(at));
		}
		const Cargo::Totals totals = Cargo::Sum(world, crew);
		const DriverState* cab = GetCab(world);
		append(",\"count\":%d,\"feeding\":%d,\"leaks\":%d,\"o2\":%.3f,\"cab\":%s}", mites, feeding, totals.LeakingTanks,
			totals.OxygenCapacityKg > 0 ? totals.OxygenKg / totals.OxygenCapacityKg : 0.0f, cab && cab->InCab ? "true" : "false");
		page->SetJson("bay", json);
	}
}

void DriverLauncherSystem::OnStart(World&)
{
	m_Launched = false;
}

void DriverLauncherSystem::Update(World& world, float)
{
	const MatchStateComponent* state = GetState(world);
	DriverNetComponent* link = GetLink(world);
	if (m_Launched || !state || state->Phase != MatchPhase::Racing || !link)
		return;
	const bool host = GameNet::IsHost(world);
	// Away from the host, the cab waits for the host's weather and asteroids.
	if (!host && !link->PackageReady)
		return;
	m_Launched = true;

	const Entity local = FindLocalMember(world);
	const CrewMemberComponent* member = local == NullEntity ? nullptr : world.TryGet<CrewMemberComponent>(local);
	// The host runs the truck for everyone. Anyone else only needs the cab when they are on its crew.
	const bool onCrew = member && member->Slot >= 0 && member->Crew == TruckCrew;
	if (!host && !onCrew)
		return;
	if (FindByName(world, TruckName) == NullEntity)
	{
		FP_LOG_WARN("Driver bridge: the scene has no entity named Truck, so there is nothing to drive");
		return;
	}

	DriverNetSetup net;
	net.Authority = host;
	net.HasPackage = !host;
	if (!host)
		net.Package = link->Package;

	if (TruckResourcesComponent* resources = Cargo::FindResources(world, TruckCrew))
		resources->DriverCab = true;
	// Added in this order so the bridge runs right before and right after the Driver's systems.
	world.AddSystem<CargoBridgeInSystem>(TruckCrew);
	link->Driver = world.AddSystem<DriverSystem>(std::move(net));
	world.AddSystem<CargoBridgeOutSystem>(TruckCrew);
	// After the Driver's systems, so the hatch's ghosts follow where the cab is this frame.
	world.AddSystem<TransitSystem>();
	// And so the stars stay centered on the cab camera, which the Driver's systems just moved.
	world.AddSystem<StarfieldSystem>();
	if (host)
		SendPackage(world, link->Driver->GetPackage());

	// Either crewmate can fly or defend. The Driver's seat starts in the cab, the Defender's in the
	// bay, and anyone not on the truck's crew stays out of its cab.
	if (DriverState* cab = GetCab(world))
	{
		cab->InCab = onCrew && member->Role == CrewRole::Driver;
		// Look the way the player was looking around the cab during the countdown.
		if (const StartLookComponent* look = world.TryGet<StartLookComponent>(local); look && cab->InCab)
		{
			cab->Yaw = look->Yaw;
			cab->Pitch = look->Pitch;
		}
	}
	FP_LOG("Driver bridge: Crew %s's cab is running%s", GetCrewName(TruckCrew), host ? " (simulated here, as host)" : "");
}

void CargoBridgeInSystem::Update(World& world, float)
{
	const Entity truck = FindByName(world, TruckName);
	LifeSupport* life = truck == NullEntity ? nullptr : world.TryGet<LifeSupport>(truck);
	DriverNetComponent* link = GetLink(world);
	if (!life || !link || !link->Driver)
		return;
	const PeerId local = GameNet::GetLocalId(world);

	if (!GameNet::IsHost(world))
	{
		// The truck as the host last sent it.
		if (link->HasTruck)
		{
			world.Get<TruckState>(truck) = link->Truck;
			*life = link->Life;
		}
		link->Driver->SetWheelTaken(link->WheelHolder != 0 && link->WheelHolder != local);
		// Both sat down at once and the host gave the wheel to the other one.
		DriverState* cab = GetCab(world);
		if (cab && cab->Seated && link->WheelHolder != 0 && link->WheelHolder != local)
			cab->Seated = false;
		return;
	}

	life->Cargo = Cargo::Sum(world, m_Crew).OxygenKg;
	world.Add<CargoBridgeComponent>(truck).Before = life->Cargo;

	// Who holds the wheel: this machine's player first, then a crewmate on another machine.
	const double now = world.GetTime();
	PeerId holder = link->Driver->IsLocalSeated() ? local : 0;
	const DriverNetComponent::RemoteCrew* remoteDriver = nullptr;
	bool remoteRepairing = false;
	for (const DriverNetComponent::RemoteCrew& crew : link->Remote)
	{
		if (now - crew.LastHeard > RemoteTimeout)
			continue;
		remoteRepairing |= crew.Repairing;
		if (crew.Seated && !remoteDriver && holder == 0)
		{
			remoteDriver = &crew;
			holder = crew.Peer;
		}
	}
	link->WheelHolder = holder;
	link->Driver->SetRemoteCrew(remoteDriver != nullptr, remoteDriver ? remoteDriver->Controls : TruckControls{}, remoteRepairing);
	link->Driver->SetWheelTaken(holder != 0 && holder != local);
}

void CargoBridgeOutSystem::OnStart(World& world)
{
	// The cab's layout before it starts riding the truck: its floor is cab space's origin.
	if (const Entity floor = FindByName(world, CabFloorName); floor != NullEntity)
		m_CabOrigin = world.Get<TransformComponent>(floor).Position;
	if (const Entity seat = FindByName(world, SeatName); seat != NullEntity)
		m_Seat = world.Get<TransformComponent>(seat).Position;
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
	DriverNetComponent* link = GetLink(world);
	if (!life || !link || !link->Driver)
		return;
	const TransformComponent& truckTransform = world.Get<TransformComponent>(truck);
	SendCabSpot(world, m_Seat);
	PlaceCabCrewmates(world, *link, truckTransform, m_CabOrigin);
	SendBayHud(world, m_Crew);

	if (!GameNet::IsHost(world))
	{
		// What this player did, for the host's truck.
		NetWriter input = GameNet::Begin(GameNet::Message::CrewInput);
		const TruckControls controls = link->Driver->GetLocalControls();
		input.WriteBool(link->Driver->IsLocalSeated());
		input.WriteBool(link->Driver->IsLocalRepairing());
		input.WriteFloat(controls.Aim.X);
		input.WriteFloat(controls.Aim.Y);
		GameNet::SendToHost(world, input, NetChannel::Sequenced);
		if (!link->Results.empty())
		{
			link->Driver->ShowResults(world, link->Results.c_str());
			link->Results.clear();
		}
		return;
	}

	// Spent comes out of the tanks; won back goes into them.
	if (const CargoBridgeComponent* bridge = world.TryGet<CargoBridgeComponent>(truck))
	{
		const float spent = bridge->Before - life->Cargo;
		if (spent > 0)
			Cargo::Draw(world, m_Crew, CargoKind::Oxygen, spent);
		else if (spent < 0)
			Cargo::Refill(world, m_Crew, CargoKind::Oxygen, -spent);
	}

	// The truck for everyone else, every frame.
	NetWriter message = GameNet::Begin(GameNet::Message::TruckState);
	message.WriteBytes(&world.Get<TruckState>(truck), sizeof(TruckState));
	message.WriteBytes(life, sizeof(LifeSupport));
	message.WriteU64(link->WheelHolder);
	GameNet::Send(world, message, NetChannel::Sequenced);

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
	const char* reason = delivered ? "DELIVERED" : life->ColonyTimeLeft <= 0 ? "THE COLONY RAN OUT OF AIR" : "CARGO EMPTY: NOTHING LEFT TO DELIVER";
	NetWriter results = GameNet::Begin(GameNet::Message::DriverResults);
	results.WriteString(reason);
	GameNet::Send(world, results, NetChannel::Reliable);
	FP_LOG("Match: over. %s", reason);
}
