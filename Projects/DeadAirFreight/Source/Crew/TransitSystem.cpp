#include "Crew/TransitSystem.h"
#include "Audio/Sounds.h"
#include "Driver/Cab.h"
#include "Enemies/Airlock.h"
#include "Driver/DriverSettings.h"
#include "Match/MatchQueries.h"
#include "Net/GameNet.h"

#include <cmath>

using namespace FP;
using namespace MatchQueries;

namespace
{
	// The Driver scene's names for the cab hatch and the player's body in the cab.
	constexpr const char* CabHatchName = "CabHatch";
	constexpr const char* CabDriverName = "Driver";
	// Players arrive this far in from a hatch, facing into the room, just out of its reach so a
	// second press of E does not send them straight back.
	constexpr float ArriveStep = 2.3f;

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

	void ClimbDown(World& world, Entity body, int32 crew, DriverState& cab, Vec3 bayHatch)
	{
		cab.InCab = false;
		cab.Seated = false;
		TransformComponent& transform = world.Get<TransformComponent>(body);
		// The bay hatch is in the back wall; step forward from it, toward the tanks. The hatch's middle
		// is a metre up, and the body's middle half its height.
		transform.Scale = WalkerSize;
		transform.Position = { bayHatch.X, bayHatch.Y - 1 + WalkerSize.Y * 0.5f, bayHatch.Z - ArriveStep };
		transform.Rotation = {};
		OnFootComponent& onFoot = world.Add<OnFootComponent>(body);
		onFoot.Crew = crew;
		onFoot.Interior = "Bay";
		FP_LOG("Transit: Crew %s climbed down to the cargo bay", GetCrewName(crew));
		Sounds::Play(world, SoundCue::Hatch);
	}

	void ClimbUp(World& world, Entity body, int32 crew, DriverState& cab, Vec3 cabHatch)
	{
		// The laser's beam and crosshair belong to this walk in the bay only.
		const OnFootComponent& onFoot = world.Get<OnFootComponent>(body);
		if (world.IsValid(onFoot.Beam))
			world.DestroyEntity(onFoot.Beam);
		if (world.IsValid(onFoot.Crosshair))
			world.DestroyEntity(onFoot.Crosshair);
		world.Remove<OnFootComponent>(body);

		// The cab hatch is in the back wall; step forward from it, toward the wheel. The cab keeps the
		// player's height at its floor.
		cab.InCab = true;
		cab.Seated = false;
		cab.Position = { cabHatch.X, cab.Position.Y, cabHatch.Z - ArriveStep };
		cab.Yaw = 0;
		cab.Pitch = 0;
		FP_LOG("Transit: Crew %s climbed up to the cab", GetCrewName(crew));
		Sounds::Play(world, SoundCue::Hatch);
	}
}

void TransitSystem::OnStart(World& world)
{
	m_NearHatch = false;
	const Entity hatch = FindByName(world, CabHatchName);
	m_HasCabHatch = hatch != NullEntity;
	if (m_HasCabHatch)
		m_CabHatch = world.Get<TransformComponent>(hatch).Position;
	DriverSettings settings;
	if (LoadDriverSettings(Paths::Content("Settings/Driver.settings").CStr(), settings))
		m_Reach = settings.InteractRange;
}

void TransitSystem::Update(World& world, float)
{
	const MatchStateComponent* state = GetState(world);
	const Entity body = FindLocalMember(world);
	if (!state || state->Phase != MatchPhase::Racing || body == NullEntity || !m_HasCabHatch)
		return;
	const CrewMemberComponent& member = world.Get<CrewMemberComponent>(body);
	// The cab exists for this player once the Driver's systems are running.
	const Entity cabBody = FindByName(world, CabDriverName);
	DriverState* cab = cabBody == NullEntity ? nullptr : world.TryGet<DriverState>(cabBody);
	const Entity bayHatch = FindBayHatch(world, member.Crew);
	if (member.Slot < 0 || !cab || bayHatch == NullEntity)
		return;
	const Vec3 bayHatchAt = world.Get<TransformComponent>(bayHatch).Position;

	bool near = false;
	if (world.Has<OnFootComponent>(body))
		near = FlatDistance(world.Get<TransformComponent>(body).Position, bayHatchAt) <= m_Reach;
	else if (cab->InCab && !cab->Seated)
		near = FlatDistance(cab->Position, m_CabHatch) <= m_Reach;
	const bool inCab = !world.Has<OnFootComponent>(body);
	if (near && !m_NearHatch)
		FP_LOG("Transit: press E to climb %s", inCab ? "down to the cargo bay, R to vent it" : "up to the cab");
	m_NearHatch = near;
	if (near && inCab && Input::WasKeyPressed(SDL_SCANCODE_R))
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
	if (!near || !Input::WasKeyPressed(SDL_SCANCODE_E))
		return;

	if (world.Has<OnFootComponent>(body))
		ClimbUp(world, body, member.Crew, *cab, m_CabHatch);
	else
		ClimbDown(world, body, member.Crew, *cab, bayHatchAt);
	m_NearHatch = false;
}
