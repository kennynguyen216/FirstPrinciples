#include "Bridge/DriverBridge.h"
#include "Crew/CrewComponents.h"
#include "Crew/TransitSystem.h"
#include "Effects/FreeCam.h"
#include "Effects/Starfield.h"
#include "Driver/Cab.h"
#include "Driver/DriverSystem.h"
#include "Enemies/SiphonComponents.h"
#include "Engine/UI/UISystem.h"
#include "LifeSupport/Cargo.h"
#include "Match/MatchQueries.h"
#include "Net/DriverNet.h"
#include "Net/GameNet.h"
#include "Title/TitleComponents.h"

#include <algorithm>
#include <cmath>
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
	constexpr const char* RaceHudPage = "UI/RaceHud.html";
	// Pieces of the match package, in characters, small enough for one network message.
	constexpr int32 PackagePiece = 800;
	// The trucks start side by side this far apart, Crew A on the left. A truck is about 7 wide.
	constexpr float LaneSpacing = 14;
	// A crewmate on another machine not heard from for this long has left the wheel, or the cab.
	constexpr double RemoteTimeout = 1.0;
	// A rival not heard from for this long has gone, and stops being drawn.
	constexpr double RivalTimeout = 3.0;
	// Between messages a rival carries on at its last speed, but no further than this ahead.
	constexpr double MostExtrapolation = 0.5;
	// How quickly a drawn rival catches up with where it should be. Further off than the snap
	// distance, it jumps there.
	constexpr float RivalCatchUp = 15;
	constexpr float RivalSnap = 50;
	// For bumping, a truck is a capsule along its length: from just behind the nose to the end of
	// the trailer, as thick as the hull. Its origin sits near the nose and the trailer trails behind.
	constexpr float HullRadius = 3.5f;
	constexpr float HullFront = 1.0f;
	constexpr float HullBack = 21.5f;
	// Each truck pushes itself this share of the way out (the other pushes itself too), and loses
	// this much of its speed when it hits.
	constexpr float BumpPush = 0.6f;
	constexpr float BumpSpeedKept = 0.6f;
	// A bump shakes everyone aboard nearly as hard as a laser at full heat, dying away over this long.
	constexpr float BumpShake = 0.9f;
	constexpr float BumpShakeSeconds = 0.7f;
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

	// The seat that runs a crew's truck: its first one taken, or -1 for a crew nobody is on.
	int32 CaptainSlot(const MatchStateComponent& state, int32 crew)
	{
		for (int32 slot = crew; slot < MaxSlots; slot += CrewCount)
		{
			if (state.SeatTaken[slot])
				return slot;
		}
		return -1;
	}

	// The end of the match, for everyone: the host's page shows it, and the others are told.
	void EndMatch(World& world, MatchStateComponent& state, int32 winner, const std::string& reason)
	{
		state.Phase = MatchPhase::Results;
		state.WinnerCrew = winner;
		if (DriverNetComponent* link = GetLink(world))
			link->Results = reason;
		NetWriter results = GameNet::Begin(GameNet::Message::DriverResults);
		results.WriteString(reason);
		GameNet::Send(world, results, NetChannel::Reliable);
		FP_LOG("Match: over. %s", reason.c_str());
	}

	// Tells everyone where this player stands in the cab, in cab space: where they walk, or the seat.
	void SendCabSpot(World& world, int32 crew, Vec3 seat)
	{
		const DriverState* cab = GetCab(world);
		const Entity local = FindLocalMember(world);
		const bool inCab = cab && cab->InCab && local != NullEntity && !world.Has<OnFootComponent>(local);
		NetWriter message = GameNet::Begin(GameNet::Message::CrewInCab);
		message.WriteU8(static_cast<uint8>(crew));
		message.WriteBool(inCab);
		message.WriteVec3(cab && cab->Seated ? Vec3{ seat.X, cab->Position.Y, seat.Z } : cab ? cab->Position : Vec3{});
		message.WriteFloat(cab && !cab->Seated ? cab->Yaw : 0);
		GameNet::Send(world, message, NetChannel::Sequenced);
	}

	// Stands each crewmate who is in this crew's cab inside this machine's copy of it.
	void PlaceCabCrewmates(World& world, DriverNetComponent& link, int32 crew, const TransformComponent& truck, Vec3 cabOrigin)
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
			if (crewmate.Crew != crew || !crewmate.InCab || world.GetTime() - crewmate.LastHeard > RemoteTimeout)
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

	// Closest points between two segments, p1-q1 and p2-q2.
	void ClosestPoints(Vec3 p1, Vec3 q1, Vec3 p2, Vec3 q2, Vec3& c1, Vec3& c2)
	{
		const Vec3 d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
		const float a = Vec3::Dot(d1, d1), e = Vec3::Dot(d2, d2), f = Vec3::Dot(d2, r);
		float s = 0, t = 0;
		if (a > 1e-6f && e > 1e-6f)
		{
			const float b = Vec3::Dot(d1, d2), c = Vec3::Dot(d1, r);
			const float denominator = a * e - b * b;
			s = denominator > 1e-6f ? std::clamp((b * f - c * e) / denominator, 0.0f, 1.0f) : 0;
			t = (b * s + f) / e;
			if (t < 0)
			{
				t = 0;
				s = std::clamp(-c / a, 0.0f, 1.0f);
			}
			else if (t > 1)
			{
				t = 1;
				s = std::clamp((b - c) / a, 0.0f, 1.0f);
			}
		}
		c1 = p1 + d1 * s;
		c2 = p2 + d2 * t;
	}

	// Smooth wandering in -1 to 1, from a few sines that never line up. The laser's shake uses the same.
	float Wander(float t, float seed)
	{
		return (std::sin(t + seed) + 0.6f * std::sin(t * 2.3f + seed * 1.7f) + 0.3f * std::sin(t * 4.7f + seed * 0.3f)) / 1.9f;
	}

	// Whether this truck is against a rival's hull. The captain also pushes it back out; a
	// crewmate's machine only feels it, since its truck is the captain's.
	bool BumpRivals(World& world, const DriverNetComponent& link, int32 crew, TruckState& state, const TransformComponent& truck, bool push)
	{
		const Vec3 forward = truck.GetRotation().Rotate({ 0, 0, -1 });
		bool touching = false;
		for (int32 other = 0; other < CrewCount; ++other)
		{
			const DriverNetComponent::Pose& pose = link.Trucks[other];
			const double age = world.GetTime() - pose.Time;
			if (other == crew || !pose.Heard || age > RivalTimeout)
				continue;
			const Vec3 rivalAt = pose.Position + pose.Velocity * static_cast<float>(std::min(age, MostExtrapolation));
			const Vec3 rivalForward = Quat::FromEuler(pose.Rotation).Rotate({ 0, 0, -1 });
			Vec3 mine, theirs;
			ClosestPoints(state.Position + forward * HullFront, state.Position - forward * HullBack,
				rivalAt + rivalForward * HullFront, rivalAt - rivalForward * HullBack, mine, theirs);
			Vec3 apart = mine - theirs;
			const float distance = apart.Length();
			if (distance >= HullRadius * 2)
				continue;
			touching = true;
			if (!push)
				continue;
			apart = distance > 1e-3f ? apart * (1 / distance) : truck.GetRotation().Rotate({ 1, 0, 0 });
			state.Position += apart * ((HullRadius * 2 - distance) * BumpPush);
		}
		return touching;
	}

	// Where a point lands on screen, from -1 to 1 across and up, or false when it is out of view
	// (then the result still says which way it lies). The same as the Driver's colony arrow.
	bool ToScreen(const TransformComponent& camera, float fieldOfView, float aspect, Vec3 point, Vec2& screen)
	{
		const Quat rotation = camera.GetRotation();
		const Vec3 local = Quat{ -rotation.X, -rotation.Y, -rotation.Z, rotation.W }.Rotate(point - camera.Position);
		if (local.Z >= 0)
		{
			screen = Vec2{ local.X, local.Y };
			return false;
		}
		const float halfHeight = std::tan(fieldOfView * 0.5f * DegToRad) * -local.Z;
		screen = Vec2{ local.X / aspect, local.Y } * (1 / halfHeight);
		return std::abs(screen.X) <= 1 && std::abs(screen.Y) <= 1;
	}
}

namespace DriverBridge
{
	void SpendCargo(World& world, int32 crew, float kg)
	{
		if (kg > 0)
			Cargo::Draw(world, crew, CargoKind::Oxygen, kg);
		else if (kg < 0)
			Cargo::Refill(world, crew, CargoKind::Oxygen, -kg);
	}

	void FinishTruck(World& world, int32 crew, bool delivered, float cargoKg)
	{
		MatchStateComponent* state = GetState(world);
		DriverNetComponent* link = GetLink(world);
		if (!state || !link || state->Phase != MatchPhase::Racing || link->Finished[crew])
			return;
		link->Finished[crew] = true;
		if (delivered)
		{
			state->Crews[crew].Arrived = true;
			state->Crews[crew].ArrivalTime = state->RaceTime;
			state->Crews[crew].OxygenDeliveredKg = cargoKg;
			EndMatch(world, *state, crew, std::string("CREW ") + GetCrewName(crew) + " DELIVERED FIRST");
			return;
		}
		FP_LOG("Match: Crew %s is out of the race", GetCrewName(crew));
		for (int32 other = 0; other < CrewCount; ++other)
		{
			if (CaptainSlot(*state, other) >= 0 && !link->Finished[other])
				return; // someone is still racing
		}
		EndMatch(world, *state, -1, "NOBODY DELIVERED");
	}

	void ReceivePose(World& world, DriverNetComponent& link, int32 crew, Vec3 position, Vec3 velocity, Vec3 rotation)
	{
		DriverNetComponent::Pose& pose = link.Trucks[crew];
		pose.Heard = true;
		pose.Position = position;
		pose.Velocity = velocity;
		pose.Rotation = rotation;
		pose.Time = world.GetTime();
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
	// At Results too: a match over before this machine's truck got going still shows its results.
	if (m_Launched || !state || (state->Phase != MatchPhase::Racing && state->Phase != MatchPhase::Results) || !link)
		return;
	const bool host = GameNet::IsHost(world);
	// Away from the host, the cab waits for the host's weather and asteroids.
	if (!host && !link->PackageReady)
		return;
	m_Launched = true;

	const Entity local = FindLocalMember(world);
	const CrewMemberComponent* member = local == NullEntity ? nullptr : world.TryGet<CrewMemberComponent>(local);
	if (!member || member->Slot < 0)
		return; // watching, with no truck of their own
	const int32 crew = member->Crew;
	const bool captain = CaptainSlot(*state, crew) == member->Slot;
	const Entity truck = FindByName(world, TruckName);
	if (truck == NullEntity)
	{
		FP_LOG_WARN("Driver bridge: the scene has no entity named Truck, so there is nothing to drive");
		return;
	}

	// This crew's truck lines up in its lane before the Driver's systems read where it is.
	world.Get<TransformComponent>(truck).Position.X += (static_cast<float>(crew) - (CrewCount - 1) * 0.5f) * LaneSpacing;

	DriverNetSetup net;
	net.Authority = captain;
	net.HasPackage = !host;
	if (!host)
		net.Package = link->Package;

	// Every truck's cab breathes for its crew. The host runs the bays, so it hears this for every crew.
	for (int32 other = 0; other < CrewCount; ++other)
	{
		if (other == crew || (host && CaptainSlot(*state, other) >= 0))
		{
			if (TruckResourcesComponent* resources = Cargo::FindResources(world, other))
				resources->DriverCab = true;
		}
	}

	// The rivals first, while the truck still has the model they copy; the Driver's systems take it
	// off this crew's own truck, which its players ride inside.
	world.AddSystem<RivalTruckSystem>(crew);
	// Added in this order so the bridge runs right before and right after the Driver's systems.
	world.AddSystem<CargoBridgeInSystem>(crew, captain);
	link->Driver = world.AddSystem<DriverSystem>(std::move(net));
	world.AddSystem<CargoBridgeOutSystem>(crew, captain);
	// After the Driver's systems, so the hatch's ghosts follow where the cab is this frame.
	world.AddSystem<TransitSystem>();
	// And so the stars stay centered on the cab camera, which the Driver's systems just moved.
	world.AddSystem<StarfieldSystem>();
	// Last of all, so the trailer camera overrides the cab camera.
	world.AddSystem<FreeCamSystem>();
	if (host)
		SendPackage(world, link->Driver->GetPackage());

	// The Driver's seat starts in the cab, the crewmate's in the cargo bay.
	if (DriverState* cab = GetCab(world))
	{
		cab->InCab = member->Role == CrewRole::Driver;
		// Look the way the player was looking around the cab during the countdown.
		if (const StartLookComponent* look = world.TryGet<StartLookComponent>(local); look && cab->InCab)
		{
			cab->Yaw = look->Yaw;
			cab->Pitch = look->Pitch;
		}
	}
	FP_LOG("Driver bridge: Crew %s's truck is %s", GetCrewName(crew), captain ? "running here" : "running on the captain's machine");
}

void CargoBridgeInSystem::Update(World& world, float)
{
	const Entity truck = FindByName(world, TruckName);
	LifeSupport* life = truck == NullEntity ? nullptr : world.TryGet<LifeSupport>(truck);
	DriverNetComponent* link = GetLink(world);
	if (!life || !link || !link->Driver)
		return;
	const PeerId local = GameNet::GetLocalId(world);
	DriverNetComponent::Truck& shared = link->Crews[m_Crew];

	if (!m_Captain)
	{
		// The truck as the captain last sent it.
		if (shared.Heard)
		{
			world.Get<TruckState>(truck) = shared.State;
			*life = shared.Life;
		}
		link->Driver->SetWheelTaken(shared.WheelHolder != 0 && shared.WheelHolder != local);
		// Both sat down at once and the captain gave the wheel to the other one.
		DriverState* cab = GetCab(world);
		if (cab && cab->Seated && shared.WheelHolder != 0 && shared.WheelHolder != local)
			cab->Seated = false;
		return;
	}

	life->Cargo = Cargo::Sum(world, m_Crew).OxygenKg;
	world.Add<CargoBridgeComponent>(truck).Before = life->Cargo;

	// Who holds the wheel: the captain first, then a crewmate on another machine.
	const double now = world.GetTime();
	PeerId holder = link->Driver->IsLocalSeated() ? local : 0;
	const DriverNetComponent::CrewInput* remoteDriver = nullptr;
	bool remoteRepairing = false;
	for (const DriverNetComponent::CrewInput& input : link->Inputs)
	{
		if (input.Crew != m_Crew || now - input.LastHeard > RemoteTimeout)
			continue;
		remoteRepairing |= input.Repairing;
		if (input.Seated && !remoteDriver && !link->Driver->IsLocalSeated())
		{
			remoteDriver = &input;
			holder = input.Peer;
		}
	}
	shared.WheelHolder = holder;
	link->Driver->SetRemoteCrew(remoteDriver != nullptr, remoteDriver ? remoteDriver->Controls : TruckControls{}, remoteRepairing);
	link->Driver->SetWheelTaken(remoteDriver != nullptr);
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
	m_Reported = false;
	m_Moved = false;
	m_Touching = false;
	m_Shake = 0;
}

void CargoBridgeOutSystem::Update(World& world, float deltaTime)
{
	const Entity truck = FindByName(world, TruckName);
	LifeSupport* life = truck == NullEntity ? nullptr : world.TryGet<LifeSupport>(truck);
	DriverNetComponent* link = GetLink(world);
	if (!life || !link || !link->Driver)
		return;
	const TransformComponent& truckTransform = world.Get<TransformComponent>(truck);
	const bool host = GameNet::IsHost(world);
	SendCabSpot(world, m_Crew, m_Seat);
	PlaceCabCrewmates(world, *link, m_Crew, truckTransform, m_CabOrigin);
	SendBayHud(world, m_Crew);

	// Results from the host (or from this machine, as host) end this player's race too, if the
	// truck is still going.
	if (!link->Results.empty())
	{
		link->Driver->ShowResults(world, link->Results.c_str());
		link->Results.clear();
	}

	// Bump off the rival: the captain pushes the truck out of its hull and it loses speed in the
	// knock (the rival's captain does the same for theirs, so between them they come apart), and
	// everyone aboard gets shaken.
	TruckState& state = world.Get<TruckState>(truck);
	const bool touching = BumpRivals(world, *link, m_Crew, state, truckTransform, m_Captain);
	if (touching && !m_Touching)
	{
		m_Shake = BumpShake;
		if (m_Captain)
		{
			state.Speed *= BumpSpeedKept;
			FP_LOG("Truck: Crew %s bumped the rival truck", GetCrewName(m_Crew));
		}
	}
	m_Touching = touching;
	Shake(world, deltaTime);

	if (!m_Captain)
	{
		// What this player did, for the captain's truck.
		NetWriter input = GameNet::Begin(GameNet::Message::CrewInput);
		const TruckControls controls = link->Driver->GetLocalControls();
		input.WriteU8(static_cast<uint8>(m_Crew));
		input.WriteBool(link->Driver->IsLocalSeated());
		input.WriteBool(link->Driver->IsLocalRepairing());
		input.WriteFloat(controls.Aim.X);
		input.WriteFloat(controls.Aim.Y);
		GameNet::Send(world, input, NetChannel::Sequenced);
		return;
	}

	// Spent comes out of the tanks; won back goes into them. The host runs the tanks.
	if (const CargoBridgeComponent* bridge = world.TryGet<CargoBridgeComponent>(truck))
	{
		const float spent = bridge->Before - life->Cargo;
		if (spent != 0 && host)
			DriverBridge::SpendCargo(world, m_Crew, spent);
		else if (spent != 0)
		{
			NetWriter message = GameNet::Begin(GameNet::Message::CargoSpend);
			message.WriteU8(static_cast<uint8>(m_Crew));
			message.WriteFloat(spent);
			GameNet::SendToHost(world, message, NetChannel::Reliable);
		}
	}

	// This truck for everyone: where it is and how fast it goes, for rivals to draw and bump; the
	// whole truck for crewmates to show.
	const Vec3 velocity = m_Moved && deltaTime > 0 ? (truckTransform.Position - m_LastPosition) / deltaTime : Vec3{};
	m_LastPosition = truckTransform.Position;
	m_Moved = true;
	NetWriter pose = GameNet::Begin(GameNet::Message::TruckPose);
	pose.WriteU8(static_cast<uint8>(m_Crew));
	pose.WriteVec3(truckTransform.Position);
	pose.WriteVec3(velocity);
	pose.WriteVec3(truckTransform.Rotation);
	GameNet::Send(world, pose, NetChannel::Sequenced);
	NetWriter whole = GameNet::Begin(GameNet::Message::TruckState);
	whole.WriteU8(static_cast<uint8>(m_Crew));
	whole.WriteBytes(&state, sizeof(TruckState));
	whole.WriteBytes(life, sizeof(LifeSupport));
	whole.WriteU64(link->Crews[m_Crew].WheelHolder);
	GameNet::Send(world, whole, NetChannel::Sequenced);

	// The Driver's race ends on the same three conditions the Driver's systems use. Tell the host
	// once, and it decides the match.
	const MatchStateComponent* match = GetState(world);
	if (m_Reported || !match || match->Phase != MatchPhase::Racing)
		return;
	const Entity colony = FindByName(world, ColonyName);
	const bool delivered = colony != NullEntity &&
		(truckTransform.Position - world.Get<TransformComponent>(colony).Position).Length() - m_ColonyRadius <= m_DeliveryAltitude;
	if (!delivered && life->ColonyTimeLeft > 0 && life->Cargo > 0)
		return;
	m_Reported = true;
	if (host)
		DriverBridge::FinishTruck(world, m_Crew, delivered, life->Cargo);
	else
	{
		NetWriter finished = GameNet::Begin(GameNet::Message::TruckFinished);
		finished.WriteU8(static_cast<uint8>(m_Crew));
		finished.WriteBool(delivered);
		finished.WriteFloat(life->Cargo);
		GameNet::SendToHost(world, finished, NetChannel::Reliable);
	}
}

void CargoBridgeOutSystem::Shake(World& world, float deltaTime)
{
	m_ShakeTime += deltaTime;
	m_Shake = std::max(0.0f, m_Shake - deltaTime / BumpShakeSeconds);
	if (m_Shake <= 0)
		return;
	Entity camera = world.GetActiveCamera();
	for (auto [entity, view] : world.View<CameraComponent>().each())
	{
		if (!world.IsValid(camera) && view.Primary)
			camera = entity;
	}
	if (!world.IsValid(camera))
		return;
	// As strong as the laser's shake, read from the same settings, on whichever camera this player
	// sees through: the cab's or the bay's.
	OnFootConfigComponent config;
	for (auto [entity, found] : world.View<OnFootConfigComponent>().each())
		config = found;
	const float buzz = m_ShakeTime * 28;
	TransformComponent& view = world.Get<TransformComponent>(camera);
	view.Rotation.X += config.ShakeDegrees * m_Shake * Wander(buzz, 0);
	view.Rotation.Y += config.ShakeDegrees * m_Shake * Wander(buzz, 11);
	view.Rotation.Z += config.ShakeRollDegrees * m_Shake * Wander(buzz * 0.7f, 23);
	view.Position += view.GetRotation().Rotate({ 0, 1, 0 }) * (0.03f * m_Shake * Wander(buzz * 1.3f, 37));
}

void RivalTruckSystem::OnStart(World& world)
{
	m_Rivals.clear();
	const Entity truck = FindByName(world, TruckName);
	if (truck == NullEntity)
		return;
	const MeshComponent* mesh = world.TryGet<MeshComponent>(truck);
	const MaterialComponent* materials = world.TryGet<MaterialComponent>(truck);
	for (int32 crew = 0; crew < CrewCount; ++crew)
	{
		if (crew == m_LocalCrew)
			continue;
		Rival rival;
		rival.Crew = crew;
		rival.Scale = world.Get<TransformComponent>(truck).Scale;
		rival.Entity = world.CreateEntity(String::Format("Crew %s Truck", GetCrewName(crew)));
		world.Get<TransformComponent>(rival.Entity).Scale = HiddenScale;
		if (mesh)
			world.Add<MeshComponent>(rival.Entity, *mesh);
		if (materials)
			world.Add<MaterialComponent>(rival.Entity, *materials);
		m_Rivals.push_back(rival);
	}

	// The marker page, drawn over the game under the Driver's HUD.
	m_Hud = world.CreateEntity("Race HUD");
	UIComponent& page = world.Add<UIComponent>(m_Hud);
	page.Page = RaceHudPage;
	page.Input = false;
	page.Order = 5;
}

void RivalTruckSystem::Update(World& world, float deltaTime)
{
	const DriverNetComponent* link = GetLink(world);
	if (!link)
		return;
	const double now = world.GetTime();
	for (Rival& rival : m_Rivals)
	{
		if (!world.IsValid(rival.Entity))
			continue;
		TransformComponent& transform = world.Get<TransformComponent>(rival.Entity);
		const DriverNetComponent::Pose& pose = link->Trucks[rival.Crew];
		if (!pose.Heard || now - pose.Time > RivalTimeout)
		{
			transform.Scale = HiddenScale;
			rival.Shown = false;
			continue;
		}
		const float ahead = static_cast<float>(std::min(now - pose.Time, MostExtrapolation));
		const Vec3 target = pose.Position + pose.Velocity * ahead;
		if (!rival.Shown || (target - transform.Position).Length() > RivalSnap)
			transform.Position = target;
		else
		{
			// Keep pace, then close what is left, so a fast truck does not trail behind its spot.
			transform.Position += pose.Velocity * deltaTime;
			transform.Position += (target - transform.Position) * std::min(1.0f, deltaTime * RivalCatchUp);
		}
		transform.Rotation = pose.Rotation;
		transform.Scale = rival.Scale;
		rival.Shown = true;
	}
	DrawMarkers(world);
}

void RivalTruckSystem::DrawMarkers(World& world)
{
	UIDocumentInstance* page = world.IsValid(m_Hud) ? UISystem::For(world, m_Hud) : nullptr;
	if (!page || !page->IsReady())
		return;

	// Only from the cab, where the windows look out at the race.
	const DriverState* cab = GetCab(world);
	const Entity local = FindLocalMember(world);
	const bool inCab = cab && cab->InCab && local != NullEntity && !world.Has<OnFootComponent>(local);
	Entity camera = world.GetActiveCamera();
	float fieldOfView = 60;
	for (auto [entity, view] : world.View<CameraComponent>().each())
	{
		if (!world.IsValid(camera) && view.Primary)
			camera = entity;
		if (entity == camera)
			fieldOfView = view.FieldOfView;
	}
	const UIStateComponent* size = world.TryGet<UIStateComponent>(m_Hud);
	const float aspect = size && size->Height > 0 ? static_cast<float>(size->Width) / static_cast<float>(size->Height) : 16.0f / 9;

	std::string markers = "[";
	for (const Rival& rival : m_Rivals)
	{
		if (!inCab || !rival.Shown || !world.IsValid(camera))
			continue;
		const TransformComponent& view = world.Get<TransformComponent>(camera);
		const TransformComponent& truck = world.Get<TransformComponent>(rival.Entity);
		// Aim at the middle of the hull, which trails behind its origin.
		const Vec3 middle = truck.Position - truck.GetRotation().Rotate({ 0, 0, -1 }) * ((HullBack - HullFront) * 0.5f);
		Vec2 screen;
		const bool onScreen = ToScreen(view, fieldOfView, aspect, middle, screen);
		if (!onScreen)
		{
			// Off screen it becomes an arrow at the edge, pointing the way to turn.
			const float length = std::sqrt(screen.X * screen.X + screen.Y * screen.Y);
			screen = length > 1e-6f ? screen * (1 / length) : Vec2{ 0, -1 };
		}
		char marker[160];
		std::snprintf(marker, sizeof(marker), "%s{\"crew\":\"%s\",\"x\":%.3f,\"y\":%.3f,\"on\":%s,\"meters\":%.0f}",
			markers.size() > 1 ? "," : "", GetCrewName(rival.Crew), screen.X, screen.Y, onScreen ? "true" : "false",
			(middle - view.Position).Length());
		markers += marker;
	}
	page->SetJson("rivals", markers + "]");
}
