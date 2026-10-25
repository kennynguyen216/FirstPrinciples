#include "Crew/OnFootSystem.h"
#include "Audio/Sounds.h"
#include "Crew/BayActions.h"
#include "Engine/Physics/Physics.h"
#include "Enemies/SiphonComponents.h"
#include "LifeSupport/LifeSupportComponents.h"
#include "Match/MatchQueries.h"
#include "Net/GameNet.h"

#include <algorithm>
#include <cmath>

using namespace FP;
using namespace MatchQueries;

namespace
{
	constexpr float BeamWidth = 0.05f;
	// The beam leaves from the gun, a little right of and below the eye.
	const Vec3 MuzzleOffset{ 0.25f, -0.2f, 0 };
	// A speck floating in front of the eye marks where the laser goes, until the HUD can draw one.
	// Its color shows the mode.
	constexpr float CrosshairDistance = 0.5f;
	constexpr float CrosshairSize = 0.004f;
	// The repair beam's sound repeats this often while it is held.
	constexpr float RepairSoundInterval = 0.4f;
	const char* const BeamMaterials[] = { "Materials/LaserBeam.material", "Materials/RepairBeam.material" };

	OnFootConfigComponent GetConfig(World& world)
	{
		for (auto [entity, config] : world.View<OnFootConfigComponent>().each())
			return config;
		return {};
	}

	Entity FindCamera(World& world)
	{
		for (auto [entity, transform, camera] : world.View<TransformComponent, CameraComponent>().each())
		{
			if (camera.Primary)
				return entity;
		}
		return NullEntity;
	}

	Entity MakeMarker(World& world, const char* name)
	{
		const Entity marker = world.CreateEntity(name);
		world.Get<TransformComponent>(marker).Scale = {};
		world.Add<MeshComponent>(marker).Mesh = "Cube";
		world.Add<MaterialComponent>(marker).Material = BeamMaterials[0];
		return marker;
	}

	// The local walker: an entity of ours with OnFootComponent.
	Entity FindWalker(World& world)
	{
		for (auto [entity, onFoot] : world.View<OnFootComponent>().each())
		{
			if (!IsRemote(world, entity))
				return entity;
		}
		return NullEntity;
	}

	// Stretches the beam cube from one point to another.
	void ShowBeam(World& world, Entity beam, Vec3 from, Vec3 to, LaserMode mode)
	{
		TransformComponent& transform = world.Get<TransformComponent>(beam);
		const Vec3 along = to - from;
		const float length = along.Length();
		const float flat = std::sqrt(along.X * along.X + along.Z * along.Z);
		transform.Position = from + along * 0.5f;
		transform.Rotation = { std::atan2(along.Y, flat) * RadToDeg, YawToward(from, to), 0 };
		transform.Scale = { BeamWidth, BeamWidth, length };
		world.Get<MaterialComponent>(beam).Material = BeamMaterials[static_cast<int32>(mode)];
	}

	// Keeps a walker inside their room and out of anything solid in it: walls and tanks.
	// Boxes are treated as upright, which everything in the rooms is. Jolt's character controller
	// replaces this when it lands.
	void KeepClear(World& world, const OnFootComponent& onFoot, float radius, TransformComponent& body)
	{
		Vec3 room;
		InteriorComponent interior;
		if (FindInterior(world, onFoot.Crew, onFoot.Interior.CStr(), &room, &interior) != NullEntity)
		{
			body.Position.X = std::clamp(body.Position.X, room.X - interior.HalfWidth + radius, room.X + interior.HalfWidth - radius);
			body.Position.Z = std::clamp(body.Position.Z, room.Z - interior.HalfLength + radius, room.Z + interior.HalfLength - radius);
		}

		const float feet = body.Position.Y - body.Scale.Y * 0.5f;
		const float head = body.Position.Y + body.Scale.Y * 0.5f;
		for (auto [entity, transform, collider] : world.View<TransformComponent, ColliderComponent>().each())
		{
			if (collider.Shape != "Box" || collider.Trigger)
				continue;
			const Vec3 half = collider.Size * Vec3{ std::abs(transform.Scale.X), std::abs(transform.Scale.Y), std::abs(transform.Scale.Z) } * 0.5f;
			const Vec3 center = transform.Position + collider.Offset;
			// Floors and anything above or below the walker do not block.
			if (center.Y + half.Y <= feet + 0.05f || center.Y - half.Y >= head)
				continue;
			const float halfX = half.X + radius;
			const float halfZ = half.Z + radius;
			const float dx = body.Position.X - center.X;
			const float dz = body.Position.Z - center.Z;
			if (std::abs(dx) >= halfX || std::abs(dz) >= halfZ)
				continue;
			// Out along the shallower side.
			if (halfX - std::abs(dx) < halfZ - std::abs(dz))
				body.Position.X = center.X + (dx < 0 ? -halfX : halfX);
			else
				body.Position.Z = center.Z + (dz < 0 ? -halfZ : halfZ);
		}
	}

	struct Aim
	{
		Vec3 Muzzle;
		Vec3 End;
		bool Hit = false;
		RaycastHit RayHit;
	};

	// The first thing the laser meets, through the engine's Jolt world. Skips the shooter.
	Aim Trace(World& world, const OnFootConfigComponent& config, const TransformComponent& eye, Entity shooter)
	{
		Aim aim;
		const Vec3 forward = eye.GetForward();
		aim.Muzzle = eye.Position + eye.GetRight() * MuzzleOffset.X + eye.GetUp() * MuzzleOffset.Y;
		aim.End = eye.Position + forward * config.LaserRange;
		aim.Hit = world.GetPhysics().Raycast(eye.Position, forward, config.LaserRange, aim.RayHit, shooter);
		if (aim.Hit)
			aim.End = aim.RayHit.Point;
		return aim;
	}

	void Vaporize(World& world, const OnFootConfigComponent& config, OnFootComponent& onFoot, const TransformComponent& eye, Entity shooter)
	{
		const Aim aim = Trace(world, config, eye, shooter);
		const SiphonComponent* siphon = aim.Hit ? world.TryGet<SiphonComponent>(aim.RayHit.Entity) : nullptr;
		const CargoTankComponent* tank = aim.Hit ? world.TryGet<CargoTankComponent>(aim.RayHit.Entity) : nullptr;
		if (siphon && !siphon->Active)
			siphon = nullptr;
		if (siphon || tank)
		{
			// The host applies hits. Elsewhere they go to the host, and its next bay update shows them.
			if (GameNet::IsHost(world))
			{
				if (siphon)
					BayActions::HitSiphon(world, aim.RayHit.Entity, config.LaserDamage);
				else
					BayActions::PunctureTank(world, aim.RayHit.Entity, aim.RayHit.Point); // a miss that lands on cargo
			}
			else
			{
				NetWriter message = GameNet::Begin(GameNet::Message::LaserHit);
				message.WriteBool(siphon != nullptr);
				message.WriteU8(static_cast<uint8>(siphon ? siphon->Crew : tank->Crew));
				message.WriteU8(static_cast<uint8>(siphon ? siphon->Index : tank->Index));
				message.WriteFloat(config.LaserDamage);
				message.WriteVec3(aim.RayHit.Point);
				GameNet::SendToHost(world, message, NetChannel::Reliable);
			}
		}
		Sounds::Play(world, SoundCue::LaserFire);
		ShowBeam(world, onFoot.Beam, aim.Muzzle, aim.End, LaserMode::Vaporize);
		onFoot.BeamTimeLeft = config.BeamSeconds;
		onFoot.Cooldown = config.FireInterval;
	}

	void Repair(World& world, const OnFootConfigComponent& config, OnFootComponent& onFoot, const TransformComponent& eye, Entity shooter, float deltaTime)
	{
		const Aim aim = Trace(world, config, eye, shooter);
		const CargoTankComponent* tank = aim.Hit ? world.TryGet<CargoTankComponent>(aim.RayHit.Entity) : nullptr;
		if (tank && tank->Holes > 0)
		{
			if (GameNet::IsHost(world))
				BayActions::RepairTank(world, aim.RayHit.Entity, deltaTime, aim.RayHit.Point);
			else
			{
				NetWriter message = GameNet::Begin(GameNet::Message::LaserRepair);
				message.WriteU8(static_cast<uint8>(tank->Crew));
				message.WriteU8(static_cast<uint8>(tank->Index));
				message.WriteFloat(deltaTime);
				message.WriteVec3(aim.RayHit.Point);
				GameNet::SendToHost(world, message, NetChannel::Reliable);
			}
		}

		onFoot.RepairSoundIn -= deltaTime;
		if (onFoot.RepairSoundIn <= 0)
		{
			Sounds::Play(world, SoundCue::RepairBeam);
			onFoot.RepairSoundIn = RepairSoundInterval;
		}
		// The repair beam stays on for as long as the button is held.
		ShowBeam(world, onFoot.Beam, aim.Muzzle, aim.End, LaserMode::Repair);
		onFoot.BeamTimeLeft = deltaTime * 1.5f;
	}
}

void OnFootSystem::Update(World& world, float deltaTime)
{
	const MatchStateComponent* state = GetState(world);
	if (!state || state->Phase == MatchPhase::Lobby)
		return;

	// A player seated as their crew's Defender starts the match on foot in the bay. After that
	// TransitSystem moves players between the bay and the cab.
	const Entity local = FindLocalMember(world);
	if (state->Phase == MatchPhase::Countdown && local != NullEntity && !world.Has<OnFootComponent>(local))
	{
		const CrewMemberComponent& member = world.Get<CrewMemberComponent>(local);
		if (member.Slot >= 0 && member.Role == CrewRole::Defender)
			world.Add<OnFootComponent>(local).Crew = member.Crew;
	}

	const Entity walker = FindWalker(world);
	if (walker == NullEntity)
		return;
	OnFootComponent& onFoot = world.Get<OnFootComponent>(walker);
	const OnFootConfigComponent config = GetConfig(world);
	if (onFoot.Beam == NullEntity)
	{
		onFoot.Beam = MakeMarker(world, "Laser Beam");
		onFoot.Crosshair = MakeMarker(world, "Crosshair");
	}

	TransformComponent& body = world.Get<TransformComponent>(walker);

	// Look follows the mouse, the same way the Driver's cab does. The engine locks the cursor while
	// playing and keeps reporting how far the mouse moved.
	const Vec2 delta = Input::GetMouseDelta();
	body.Rotation.Y = std::fmod(body.Rotation.Y - delta.X * config.LookSensitivity, 360.0f);
	body.Rotation.X = std::clamp(body.Rotation.X - delta.Y * config.LookSensitivity, -85.0f, 85.0f);
	body.Rotation.Z = 0;

	const bool racing = state->Phase == MatchPhase::Racing;
	if (racing)
	{
		const float yaw = body.Rotation.Y * DegToRad;
		const Vec3 forward{ -std::sin(yaw), 0, -std::cos(yaw) };
		const Vec3 right{ std::cos(yaw), 0, -std::sin(yaw) };
		const Vec2 axis = Input::GetMoveAxis();
		const Vec3 move = forward * axis.Y + right * axis.X;
		if (move.LengthSquared() > 0)
			body.Position += move.Normalized() * (config.MoveSpeed * deltaTime);
		KeepClear(world, onFoot, config.BodyRadius, body);

		if (Input::WasKeyPressed(SDL_SCANCODE_Q))
		{
			onFoot.Mode = onFoot.Mode == LaserMode::Vaporize ? LaserMode::Repair : LaserMode::Vaporize;
			FP_LOG("Laser: %s mode", onFoot.Mode == LaserMode::Vaporize ? "vaporize" : "repair");
		}
	}

	// First person: the camera sits at the eyes. The body's middle is half its height up.
	TransformComponent eye;
	eye.Position = body.Position + Vec3{ 0, config.EyeHeight - body.Scale.Y * 0.5f, 0 };
	eye.Rotation = { body.Rotation.X, body.Rotation.Y, 0 };
	if (const Entity camera = FindCamera(world); camera != NullEntity)
	{
		TransformComponent& cameraTransform = world.Get<TransformComponent>(camera);
		cameraTransform.Position = eye.Position;
		cameraTransform.Rotation = eye.Rotation;
	}
	TransformComponent& crosshair = world.Get<TransformComponent>(onFoot.Crosshair);
	crosshair.Position = eye.Position + eye.GetForward() * CrosshairDistance;
	crosshair.Rotation = eye.Rotation;
	crosshair.Scale = { CrosshairSize, CrosshairSize, CrosshairSize };
	world.Get<MaterialComponent>(onFoot.Crosshair).Material = BeamMaterials[static_cast<int32>(onFoot.Mode)];

	onFoot.Cooldown = std::max(0.0f, onFoot.Cooldown - deltaTime);
	if (racing && Input::IsMouseDown(MouseButton::Left))
	{
		if (onFoot.Mode == LaserMode::Repair)
			Repair(world, config, onFoot, eye, walker, deltaTime);
		else if (onFoot.Cooldown <= 0)
			Vaporize(world, config, onFoot, eye, walker);
	}

	if (onFoot.BeamTimeLeft > 0)
	{
		onFoot.BeamTimeLeft -= deltaTime;
		if (onFoot.BeamTimeLeft <= 0)
			world.Get<TransformComponent>(onFoot.Beam).Scale = {};
	}
}
