#include "Crew/TransitSystem.h"
#include "Driver/Cab.h"
#include "Enemies/Airlock.h"
#include "Driver/DriverSettings.h"
#include "Match/MatchQueries.h"
#include "Net/GameNet.h"

#include <algorithm>
#include <cmath>
#include <string_view>

using namespace FP;
using namespace MatchQueries;

namespace
{
	// The Driver scene's names for the cab hatch and the player's body in the cab.
	constexpr const char* CabHatchName = "CabHatch";
	constexpr const char* CabDriverName = "Driver";
	constexpr const char* VentButtonName = "CabVentButton";
	// How far short of the cab hatch a shut blast door stops the player.
	constexpr float SealedDoorMargin = 0.5f;
	const char* const VentButtonClosed = "Materials/CockpitRed.material";
	const char* const VentButtonOpen = "Materials/CockpitGreen.material";
	// The doorways face each other, so out of one is into the other: half a turn about up.
	const Quat HalfTurn = Quat::AxisAngle({ 0, 1, 0 }, 180);

	// On the ghost copies of a room, so they are never copied themselves.
	struct GhostComponent
	{
	};

	// Where a doorway is: its floor point at the outside face of the wall, and the room's facing.
	struct Doorway
	{
		Vec3 Position;
		Quat Rotation;
	};

	Doorway DoorwayOf(const TransformComponent& transform)
	{
		return { transform.Position, transform.GetRotation() };
	}

	Quat Inverse(Quat q)
	{
		return { -q.X, -q.Y, -q.Z, q.W };
	}

	// Where a point or a facing at one doorway comes out of the other.
	Vec3 Through(const Doorway& from, const Doorway& to, Vec3 point)
	{
		return to.Position + (to.Rotation * HalfTurn * Inverse(from.Rotation)).Rotate(point - from.Position);
	}

	Quat Through(const Doorway& from, const Doorway& to, Quat rotation)
	{
		return to.Rotation * HalfTurn * Inverse(from.Rotation) * rotation;
	}

	Entity FindByName(World& world, const char* name)
	{
		const Entity entity = world.FindEntityByName(name);
		return world.IsValid(entity) ? entity : NullEntity;
	}

	Entity FindBayHatch(World& world, int32 crew)
	{
		for (auto [entity, hatch] : world.View<BayHatchComponent>().each())
		{
			if (hatch.Crew == crew)
				return entity;
		}
		return NullEntity;
	}

	float FlatDistance(Vec3 a, Vec3 b)
	{
		const Vec3 offset = a - b;
		return std::sqrt(offset.X * offset.X + offset.Z * offset.Z);
	}

	bool IsShown(World& world, Entity entity)
	{
		return world.Has<MeshComponent>(entity) || world.Has<PointLightComponent>(entity);
	}

	// cabDoor is in cab space, where the cab keeps its walker.
	void IntoBay(World& world, Entity body, int32 crew, DriverState& cab, const Doorway& cabDoor, const Doorway& bayDoor)
	{
		cab.InCab = false;
		cab.Seated = false;
		TransformComponent& transform = world.Get<TransformComponent>(body);
		// The body's middle is half its height up.
		transform.Scale = WalkerSize;
		transform.Position = Through(cabDoor, bayDoor, cab.Position) + Vec3{ 0, WalkerSize.Y * 0.5f, 0 };
		const Vec3 look = Through(cabDoor, bayDoor, Quat::FromEuler({ cab.Pitch, cab.Yaw, 0 })).ToEuler();
		transform.Rotation = { look.X, look.Y, 0 };
		OnFootComponent& onFoot = world.Add<OnFootComponent>(body);
		onFoot.Crew = crew;
		onFoot.Interior = "Bay";
		FP_LOG("Transit: Crew %s went back to the cargo bay", GetCrewName(crew));
	}

	void IntoCab(World& world, Entity body, int32 crew, DriverState& cab, const Doorway& cabDoor, const Doorway& bayDoor)
	{
		// The laser's beam and crosshair belong to this walk in the bay only.
		const OnFootComponent& onFoot = world.Get<OnFootComponent>(body);
		if (world.IsValid(onFoot.Beam))
			world.DestroyEntity(onFoot.Beam);
		if (world.IsValid(onFoot.Crosshair))
			world.DestroyEntity(onFoot.Crosshair);
		world.Remove<OnFootComponent>(body);

		const TransformComponent& transform = world.Get<TransformComponent>(body);
		cab.InCab = true;
		cab.Seated = false;
		cab.Position = Through(bayDoor, cabDoor, transform.Position - Vec3{ 0, transform.Scale.Y * 0.5f, 0 });
		const Vec3 look = Through(bayDoor, cabDoor, transform.GetRotation()).ToEuler();
		cab.Pitch = look.X;
		cab.Yaw = look.Y;
		FP_LOG("Transit: Crew %s went up to the cab", GetCrewName(crew));
	}
}

void TransitSystem::OnStart(World& world)
{
	m_NearHatch = false;
	m_Ghosts.clear();
	m_CabParts.clear();
	m_CabHatchEntity = FindByName(world, CabHatchName);
	if (m_CabHatchEntity != NullEntity)
		m_CabHatch = world.Get<TransformComponent>(m_CabHatchEntity).Position;
	m_VentButtonEntity = FindByName(world, VentButtonName);
	m_VentButton = m_VentButtonEntity != NullEntity ? world.Get<TransformComponent>(m_VentButtonEntity).Position : m_CabHatch;
	// The cab is the Driver's cab parts, named the way DriverSystem finds them.
	for (auto [entity, name] : world.View<NameComponent>().each())
	{
		const std::string_view partName = name.Name.CStr();
		if ((partName.starts_with("Cab") || partName == "Wheel" || partName == "Scrubber") && IsShown(world, entity))
			m_CabParts.push_back(entity);
	}
	DriverSettings settings;
	if (LoadDriverSettings(Paths::Content("Settings/Driver.settings").CStr(), settings))
		m_Reach = settings.InteractRange;
}

void TransitSystem::Update(World& world, float)
{
	const MatchStateComponent* state = GetState(world);
	const Entity body = FindLocalMember(world);
	if (!state || state->Phase != MatchPhase::Racing || body == NullEntity || m_CabHatchEntity == NullEntity)
		return;
	const CrewMemberComponent& member = world.Get<CrewMemberComponent>(body);
	// The cab exists for this player once the Driver's systems are running.
	const Entity cabBody = FindByName(world, CabDriverName);
	DriverState* cab = cabBody == NullEntity ? nullptr : world.TryGet<DriverState>(cabBody);
	const Entity bayHatch = FindBayHatch(world, member.Crew);
	Vec3 room;
	InteriorComponent bay;
	if (member.Slot < 0 || !cab || bayHatch == NullEntity || FindInterior(world, member.Crew, "Bay", &room, &bay) == NullEntity)
		return;
	const Doorway bayDoor = DoorwayOf(world.Get<TransformComponent>(bayHatch));
	const Doorway cabSpaceDoor{ m_CabHatch, {} };

	const bool near = !world.Has<OnFootComponent>(body) && cab->InCab && !cab->Seated && FlatDistance(cab->Position, m_VentButton) <= m_Reach;
	if (near && !m_NearHatch)
		FP_LOG("Transit: press R to vent the cargo bay");
	m_NearHatch = near;
	if (near && Input::WasKeyPressed(SDL_SCANCODE_R))
	{
		// The host vents. Elsewhere the request goes to it.
		if (!GameNet::IsHost(world))
		{
			NetWriter message = GameNet::Begin(GameNet::Message::Vent);
			message.WriteU8(static_cast<uint8>(member.Crew));
			GameNet::SendToHost(world, message, NetChannel::Reliable);
		}
		else if (!Airlock::Vent(world, member.Crew))
			FP_LOG("Airlock: still cycling");
	}
	// The button lights green while the airlock is open.
	if (MaterialComponent* button = m_VentButtonEntity != NullEntity ? world.TryGet<MaterialComponent>(m_VentButtonEntity) : nullptr)
		button->Material = Airlock::IsOpen(world, member.Crew) ? VentButtonOpen : VentButtonClosed;

	// Both hatches are in their room's back wall (+Z). Past one is the other room, unless the blast
	// door between them is down, when the cab's doorway is a wall like the bay's.
	const bool sealed = Airlock::IsSealed(world, member.Crew);
	if (sealed && !world.Has<OnFootComponent>(body) && cab->InCab && !cab->Seated)
		cab->Position.Z = std::min(cab->Position.Z, m_CabHatch.Z - SealedDoorMargin);
	if (world.Has<OnFootComponent>(body))
	{
		if (!sealed && world.Get<TransformComponent>(body).Position.Z > bayDoor.Position.Z)
			IntoCab(world, body, member.Crew, *cab, cabSpaceDoor, bayDoor);
	}
	else if (!sealed && cab->InCab && !cab->Seated && cab->Position.Z > m_CabHatch.Z)
	{
		IntoBay(world, body, member.Crew, *cab, cabSpaceDoor, bayDoor);
	}

	// Ghosts: the bay behind the cab's doorway, where the cab has ridden with the truck this frame,
	// and the cab behind the bay's.
	const Doorway cabDoor = DoorwayOf(world.Get<TransformComponent>(m_CabHatchEntity));
	auto show = [&](Entity source, const Doorway& from, const Doorway& to)
	{
		const TransformComponent placed = world.Get<TransformComponent>(source);
		Ghost& ghost = m_Ghosts[source];
		if (!world.IsValid(ghost.Entity))
		{
			ghost.Entity = world.CreateEntity("Ghost");
			world.Add<GhostComponent>(ghost.Entity);
		}
		ghost.Seen = true;
		TransformComponent& transform = world.Get<TransformComponent>(ghost.Entity);
		transform.Position = Through(from, to, placed.Position);
		transform.Rotation = Through(from, to, placed.GetRotation()).ToEuler();
		transform.Scale = placed.Scale;
		// Copied every frame: tanks change material as they leak, lights dim with the cab's power.
		if (const MeshComponent* mesh = world.TryGet<MeshComponent>(source))
			world.Add<MeshComponent>(ghost.Entity, MeshComponent{ *mesh });
		if (const MaterialComponent* material = world.TryGet<MaterialComponent>(source))
			world.Add<MaterialComponent>(ghost.Entity, MaterialComponent{ *material });
		if (const PointLightComponent* light = world.TryGet<PointLightComponent>(source))
			world.Add<PointLightComponent>(ghost.Entity, PointLightComponent{ *light });
	};

	for (Entity part : m_CabParts)
	{
		if (world.IsValid(part))
			show(part, cabDoor, bayDoor);
	}
	// The bay is whatever is in it right now: walls, tanks, creatures, beams.
	std::vector<Entity> bayParts;
	for (auto [entity, transform] : world.View<TransformComponent>().each())
	{
		const Vec3 at = transform.Position - room;
		if (std::abs(at.X) <= bay.HalfWidth + 1 && std::abs(at.Z) <= bay.HalfLength + 1 && at.Y >= -1 && at.Y <= 8
			&& IsShown(world, entity) && !world.Has<GhostComponent>(entity))
			bayParts.push_back(entity);
	}
	for (Entity part : bayParts)
		show(part, bayDoor, cabDoor);

	for (auto it = m_Ghosts.begin(); it != m_Ghosts.end();)
	{
		if (it->second.Seen)
		{
			it->second.Seen = false;
			++it;
			continue;
		}
		if (world.IsValid(it->second.Entity))
			world.DestroyEntity(it->second.Entity);
		it = m_Ghosts.erase(it);
	}
}
