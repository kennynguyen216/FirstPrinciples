#include "Bay/DefenderSystem.h"
#include "Enemies/SiphonSystems.h"
#include "LifeSupport/Cargo.h"
#include "Match/MatchQueries.h"
#include "Physics/PhysicsQueries.h"

#include <algorithm>
#include <cmath>

using namespace FP;
using namespace MatchQueries;

namespace
{
	constexpr float BeamWidth = 0.05f;
	// A speck floating in front of the eye marks where shots go, until the HUD can draw one.
	constexpr float CrosshairDistance = 0.5f;
	constexpr float CrosshairSize = 0.004f;
	// The beam leaves from the gun, a little right of and below the eye.
	const Vec3 MuzzleOffset{ 0.25f, -0.2f, 0 };

	DefenderConfigComponent GetConfig(World& world)
	{
		for (auto [entity, config] : world.View<DefenderConfigComponent>().each())
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

	Entity MakeBeam(World& world)
	{
		const Entity beam = world.CreateEntity("Laser Beam");
		world.Get<TransformComponent>(beam).Scale = {};
		world.Add<MeshComponent>(beam).Mesh = "Cube";
		world.Add<MaterialComponent>(beam).Material = "Materials/LaserBeam.material";
		return beam;
	}

	// Stretches the beam cube from one point to another.
	void ShowBeam(World& world, Entity beam, Vec3 from, Vec3 to)
	{
		TransformComponent& transform = world.Get<TransformComponent>(beam);
		const Vec3 along = to - from;
		const float length = along.Length();
		const float flat = std::sqrt(along.X * along.X + along.Z * along.Z);
		transform.Position = from + along * 0.5f;
		transform.Rotation = { std::atan2(along.Y, flat) * RadToDeg, YawToward(from, to), 0 };
		transform.Scale = { BeamWidth, BeamWidth, length };
	}

	// Keeps a walker inside the bay walls and out of the tanks. Jolt's character controller
	// replaces this when it lands.
	void KeepInBay(World& world, int32 crew, float radius, Vec3& position)
	{
		for (auto [entity, transform, bay] : world.View<TransformComponent, CargoBayComponent>().each())
		{
			if (bay.Crew != crew)
				continue;
			position.X = std::clamp(position.X, transform.Position.X - bay.HalfWidth + radius, transform.Position.X + bay.HalfWidth - radius);
			position.Z = std::clamp(position.Z, transform.Position.Z - bay.HalfLength + radius, transform.Position.Z + bay.HalfLength - radius);
		}
		for (auto [entity, transform, tank] : world.View<TransformComponent, CargoTankComponent>().each())
		{
			if (tank.Crew != crew)
				continue;
			const float halfX = transform.Scale.X * 0.5f + radius;
			const float halfZ = transform.Scale.Z * 0.5f + radius;
			const float dx = position.X - transform.Position.X;
			const float dz = position.Z - transform.Position.Z;
			if (std::abs(dx) >= halfX || std::abs(dz) >= halfZ)
				continue;
			// Out along the shallower side.
			if (halfX - std::abs(dx) < halfZ - std::abs(dz))
				position.X = transform.Position.X + (dx < 0 ? -halfX : halfX);
			else
				position.Z = transform.Position.Z + (dz < 0 ? -halfZ : halfZ);
		}
	}

	void Fire(World& world, const DefenderConfigComponent& config, int32 crew, const TransformComponent& eye, DefenderLaserComponent& laser)
	{
		const Vec3 forward = eye.GetForward();
		const Vec3 muzzle = eye.Position + eye.GetRight() * MuzzleOffset.X + eye.GetUp() * MuzzleOffset.Y;
		Vec3 end = eye.Position + forward * config.LaserRange;

		PhysicsQueries::RayHit hit;
		const PhysicsLayerMask mask = LayerBit(PhysicsLayer::Siphon) | LayerBit(PhysicsLayer::Cargo) | LayerBit(PhysicsLayer::Static);
		if (PhysicsQueries::Raycast(world, eye.Position, forward, config.LaserRange, mask, hit))
		{
			end = hit.Point;
			if (SiphonComponent* siphon = world.TryGet<SiphonComponent>(hit.Entity); siphon && siphon->Active)
			{
				siphon->Health -= config.LaserDamage;
				if (siphon->Health <= 0)
					Siphons::Kill(world, hit.Entity);
			}
			else if (CargoTankComponent* tank = world.TryGet<CargoTankComponent>(hit.Entity))
			{
				// A miss that lands on cargo punctures it.
				++tank->Holes;
				if (TruckResourcesComponent* resources = Cargo::FindResources(world, crew))
					++resources->TanksShot;
				FP_LOG("Defender: shot punctured %s", world.GetEntityName(hit.Entity).CStr());
			}
		}

		ShowBeam(world, laser.Beam, muzzle, end);
		laser.BeamTimeLeft = config.BeamSeconds;
		laser.Cooldown = config.FireInterval;
	}
}

void DefenderSystem::Update(World& world, float deltaTime)
{
	const MatchStateComponent* state = GetState(world);
	const Entity local = FindLocalMember(world);
	if (!state || local == NullEntity)
		return;
	const CrewMemberComponent& member = world.Get<CrewMemberComponent>(local);
	if (member.Slot < 0 || member.Role != CrewRole::Defender || state->Phase == MatchPhase::Lobby)
		return;

	const DefenderConfigComponent config = GetConfig(world);
	DefenderLaserComponent* laser = world.TryGet<DefenderLaserComponent>(local);
	if (!laser)
	{
		laser = &world.Add<DefenderLaserComponent>(local);
		laser->Beam = MakeBeam(world);
		laser->Crosshair = MakeBeam(world);
		world.Get<NameComponent>(laser->Crosshair).Name = "Crosshair";
		laser->LastMouse = Input::GetMousePosition();
	}

	TransformComponent& body = world.Get<TransformComponent>(local);

	// Look while the right button is held, like the editor.
	const Vec2 mouse = Input::GetMousePosition();
	if (Input::IsMouseDown(MouseButton::Right))
	{
		const Vec2 delta = mouse - laser->LastMouse;
		body.Rotation.Y = std::fmod(body.Rotation.Y - delta.X * config.LookSensitivity, 360.0f);
		body.Rotation.X = std::clamp(body.Rotation.X - delta.Y * config.LookSensitivity, -85.0f, 85.0f);
		body.Rotation.Z = 0;
	}
	laser->LastMouse = mouse;

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
		KeepInBay(world, member.Crew, config.BodyRadius, body.Position);
	}

	// First person: the camera sits at the Defender's eyes. The body's middle is half its height up.
	TransformComponent eye;
	eye.Position = body.Position + Vec3{ 0, config.EyeHeight - body.Scale.Y * 0.5f, 0 };
	eye.Rotation = { body.Rotation.X, body.Rotation.Y, 0 };
	if (const Entity camera = FindCamera(world); camera != NullEntity)
	{
		TransformComponent& cameraTransform = world.Get<TransformComponent>(camera);
		cameraTransform.Position = eye.Position;
		cameraTransform.Rotation = eye.Rotation;
	}
	TransformComponent& crosshair = world.Get<TransformComponent>(laser->Crosshair);
	crosshair.Position = eye.Position + eye.GetForward() * CrosshairDistance;
	crosshair.Rotation = eye.Rotation;
	crosshair.Scale = { CrosshairSize, CrosshairSize, CrosshairSize };

	laser->Cooldown = std::max(0.0f, laser->Cooldown - deltaTime);
	if (racing && laser->Cooldown <= 0 && Input::IsMouseDown(MouseButton::Left))
		Fire(world, config, member.Crew, eye, *laser);

	if (laser->BeamTimeLeft > 0)
	{
		laser->BeamTimeLeft -= deltaTime;
		if (laser->BeamTimeLeft <= 0)
			world.Get<TransformComponent>(laser->Beam).Scale = {};
	}
}
