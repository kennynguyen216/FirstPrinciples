#include "Crew/OnFootSystem.h"
#include "Audio/Sounds.h"
#include "Crew/BayActions.h"
#include "Effects/CombatEffects.h"
#include "Engine/Physics/Physics.h"
#include "Engine/UI/UISystem.h"
#include "Enemies/Airlock.h"
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
	// The race HUD page's entity, made by RivalTruckSystem once the race is on.
	constexpr const char* RaceHudName = "Race HUD";

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

	// Smooth wandering in -1 to 1, from a few sines that never line up. Drives the shake.
	float Wander(float t, float seed)
	{
		return (std::sin(t + seed) + 0.6f * std::sin(t * 2.3f + seed * 1.7f) + 0.3f * std::sin(t * 4.7f + seed * 0.3f)) / 1.9f;
	}

	// Stretches the beam cube from one point to another. A hot laser is fatter and flickers.
	void ShowBeam(World& world, Entity beam, Vec3 from, Vec3 to, LaserMode mode, float heat = 0, float time = 0)
	{
		TransformComponent& transform = world.Get<TransformComponent>(beam);
		const Vec3 along = to - from;
		const float length = along.Length();
		const float flat = std::sqrt(along.X * along.X + along.Z * along.Z);
		transform.Position = from + along * 0.5f;
		transform.Rotation = { std::atan2(along.Y, flat) * RadToDeg, YawToward(from, to), 0 };
		const float width = BeamWidth * (1 + 0.9f * heat) * (1 + (0.05f + 0.15f * heat) * std::sin(time * 70));
	transform.Scale = { width, width, length };
		world.Get<MaterialComponent>(beam).Material = BeamMaterials[static_cast<int32>(mode)];
	}

	// An overheated gun: the crosshair blinks, and the race HUD (Content/UI/RaceHud.html) warns with
	// a bar that drains as it cools.
	void ShowOverheat(World& world, const OnFootComponent& onFoot, const OnFootConfigComponent& config)
	{
		const float left = onFoot.OverheatLeft > 0 && config.OverheatSeconds > 0 ? onFoot.OverheatLeft / config.OverheatSeconds : 0;
		if (left > 0 && std::fmod(onFoot.Time, 0.25f) < 0.125f)
			world.Get<TransformComponent>(onFoot.Crosshair).Scale = {};
		const Entity hud = world.FindEntityByName(RaceHudName);
		if (UIDocumentInstance* page = world.IsValid(hud) ? UISystem::For(world, hud) : nullptr; page && page->IsReady())
			page->Set("overheat", left);
	}

	const TransformComponent* FindHatch(World& world, int32 crew)
	{
		for (auto [entity, transform, hatch] : world.View<TransformComponent, BayHatchComponent>().each())
		{
			if (hatch.Crew == crew)
				return &transform;
		}
		return nullptr;
	}

	// Keeps a walker inside their room and out of anything solid in it: walls and tanks. The bay's
	// hatch in the back wall is an open doorway the walker may step into, where TransitSystem takes
	// them through to the cab. from is where the walker stood before this step.
	// Boxes are treated as upright, which everything in the rooms is. Jolt's character controller
	// replaces this when it lands.
	void KeepClear(World& world, const OnFootComponent& onFoot, float radius, TransformComponent& body, Vec3 from)
	{
		Vec3 room;
		InteriorComponent interior;
		if (FindInterior(world, onFoot.Crew, onFoot.Interior.CStr(), &room, &interior) != NullEntity)
		{
			const float backZ = room.Z + interior.HalfLength - radius;
			const TransformComponent* hatch = onFoot.Interior == "Bay" ? FindHatch(world, onFoot.Crew) : nullptr;
			const float doorX = hatch ? hatch->Position.X : 0;
			const float doorHalfWidth = hatch ? hatch->Scale.X * 0.5f - radius : -1;
			if (from.Z > backZ)
				body.Position.X = std::clamp(body.Position.X, doorX - doorHalfWidth, doorX + doorHalfWidth);
			else
				body.Position.X = std::clamp(body.Position.X, room.X - interior.HalfWidth + radius, room.X + interior.HalfWidth - radius);
			// A shut blast door makes the doorway wall too.
			const bool atDoor = std::abs(body.Position.X - doorX) <= doorHalfWidth && !Airlock::IsSealed(world, onFoot.Crew);
			body.Position.Z = std::clamp(body.Position.Z, room.Z - interior.HalfLength + radius, atDoor ? hatch->Position.Z + 1 : backZ);
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

	// The first thing the laser meets, through the engine's Jolt world. Skips the shooter, and Mites
	// too when asked, since the repair beam has to reach the tank a Mite is sitting on.
	Aim Trace(World& world, const OnFootConfigComponent& config, const TransformComponent& eye, Entity shooter, bool throughMites = false)
	{
		Aim aim;
		const Vec3 forward = eye.GetForward();
		aim.Muzzle = eye.Position + eye.GetRight() * MuzzleOffset.X + eye.GetUp() * MuzzleOffset.Y;
		aim.End = eye.Position + forward * config.LaserRange;
		if (throughMites)
		{
			for (const RaycastHit& hit : world.GetPhysics().RaycastAll(eye.Position, forward, config.LaserRange, shooter))
			{
				if (world.Has<SiphonComponent>(hit.Entity))
					continue;
				aim.Hit = true;
				aim.RayHit = hit;
				break;
			}
		}
		else
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
		const ImpactKind kind = !aim.Hit ? ImpactKind::Nothing : siphon ? ImpactKind::Mite : tank ? ImpactKind::Tank : ImpactKind::Wall;
		Effects::LaserShot(world, aim.Muzzle, eye.GetForward(), aim.End, aim.Hit ? aim.RayHit.Normal : -eye.GetForward(), kind, onFoot.Heat);
		ShowBeam(world, onFoot.Beam, aim.Muzzle, aim.End, LaserMode::Vaporize, onFoot.Heat, onFoot.Time);
		onFoot.BeamTimeLeft = config.BeamSeconds;
		onFoot.Cooldown = config.FireInterval;
		onFoot.Kick = std::min(1.0f, onFoot.Kick + 0.6f);
	}

	void Repair(World& world, const OnFootConfigComponent& config, OnFootComponent& onFoot, const TransformComponent& eye, Entity shooter, float deltaTime)
	{
		const Aim aim = Trace(world, config, eye, shooter, true);
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
		ShowBeam(world, onFoot.Beam, aim.Muzzle, aim.End, LaserMode::Repair, 0, onFoot.Time);
		onFoot.BeamTimeLeft = deltaTime * 1.5f;

		// Green sparkle where it touches: a fierce flickering weld on a hole, a faint shimmer
		// anywhere else.
		const bool welding = tank && tank->Holes > 0;
		const Vec3 normal = aim.Hit ? aim.RayHit.Normal : -eye.GetForward();
		ParticleEmitterComponent& sparkle = Effects::Hold(world, onFoot.Sparkle, "Repair Sparkle", aim.End + normal * 0.03f);
		const float flicker = 0.5f + 0.5f * Wander(onFoot.Time * 25, 3);
		sparkle.Emitting = aim.Hit;
		sparkle.Rate = welding ? 50 + 50 * flicker : 6.0f;
		sparkle.Direction = normal;
		sparkle.Spread = welding ? 80.0f : 120.0f;
		sparkle.Speed = welding ? 1.5f + 2 * flicker : 0.4f;
		sparkle.Gravity = welding ? 0.4f : -0.2f;
		sparkle.Drag = 1.5f;
		sparkle.Lifetime = welding ? 0.35f : 0.5f;
		sparkle.StartSize = welding ? 0.05f + 0.03f * flicker : 0.04f;
		sparkle.EndSize = 0;
		sparkle.StartColor = { 0.5f, 1, 0.6f + 0.3f * flicker, 1 };
		sparkle.EndColor = { 0.1f, 0.8f, 0.3f, 0 };
		sparkle.MaxParticles = 200;
	}
}

void OnFootSystem::Update(World& world, float deltaTime)
{
	const MatchStateComponent* state = GetState(world);
	if (!state || state->Phase == MatchPhase::Lobby || state->Phase == MatchPhase::Title)
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
	const bool sprinting = Input::IsKeyDown(SDL_SCANCODE_LSHIFT);
	Sounds::SetLoop(world, LoopSlot::Footsteps, sprinting ? SoundCue::Run : SoundCue::Walk, racing && Input::GetMoveAxis().LengthSquared() > 0);
	if (racing)
	{
		const float yaw = body.Rotation.Y * DegToRad;
		const Vec3 forward{ -std::sin(yaw), 0, -std::cos(yaw) };
		const Vec3 right{ std::cos(yaw), 0, -std::sin(yaw) };
		const Vec2 axis = Input::GetMoveAxis();
		const Vec3 move = forward * axis.Y + right * axis.X;
		const Vec3 from = body.Position;
		if (move.LengthSquared() > 0)
			body.Position += move.Normalized() * (config.MoveSpeed * (sprinting ? config.SprintScale : 1) * deltaTime);
		KeepClear(world, onFoot, config.BodyRadius, body, from);

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

	onFoot.Time += deltaTime;
	onFoot.Cooldown = std::max(0.0f, onFoot.Cooldown - deltaTime);
	onFoot.OverheatLeft = std::max(0.0f, onFoot.OverheatLeft - deltaTime);
	const bool overheated = onFoot.OverheatLeft > 0;
	const bool trigger = racing && Input::IsMouseDown(MouseButton::Left);
	// An overheated gun will not shoot, but the repair beam still works.
	const bool firing = trigger && onFoot.Mode == LaserMode::Vaporize && !overheated;
	if (trigger)
	{
		if (onFoot.Mode == LaserMode::Repair)
			Repair(world, config, onFoot, eye, walker, deltaTime);
		else if (firing && onFoot.Cooldown <= 0)
			Vaporize(world, config, onFoot, eye, walker);
	}
	if (!trigger || onFoot.Mode != LaserMode::Repair)
		Effects::Release(world, onFoot.Sparkle);

	// Heat builds while the trigger is held and bleeds off after.
	onFoot.Heat = std::clamp(onFoot.Heat + (firing ? deltaTime / std::max(config.HeatUpSeconds, 0.01f) : -deltaTime / std::max(config.HeatDownSeconds, 0.01f)), 0.0f, 1.0f);
	onFoot.Kick *= std::exp(-18 * deltaTime);

	// Holding it at full heat, shaking as hard as it does, for too long overheats it: it stops
	// shooting and cools off.
	onFoot.MaxedFor = firing && onFoot.Heat >= 1 ? onFoot.MaxedFor + deltaTime : 0;
	if (onFoot.MaxedFor >= config.OverheatAfterSeconds)
	{
		onFoot.MaxedFor = 0;
		onFoot.OverheatLeft = config.OverheatSeconds;
		FP_LOG("Laser: overheated");
	}
	ShowOverheat(world, onFoot, config);

	// Embers drift off a hot gun.
	const Vec3 muzzle = eye.Position + eye.GetRight() * MuzzleOffset.X + eye.GetUp() * MuzzleOffset.Y + eye.GetForward() * 0.15f;
	if (onFoot.Heat > 0.05f)
	{
		ParticleEmitterComponent& embers = Effects::Hold(world, onFoot.Embers, "Laser Embers", muzzle);
		embers.Emitting = true;
		embers.Rate = 50 * onFoot.Heat * onFoot.Heat;
		embers.Direction = { 0, 1, 0 };
		embers.Spread = 60;
		embers.Speed = 0.3f + 0.4f * onFoot.Heat;
		embers.Gravity = -0.3f;
		embers.Drag = 1;
		embers.Lifetime = 0.45f;
		embers.StartSize = 0.015f + 0.02f * onFoot.Heat;
		embers.EndSize = 0;
		embers.StartColor = { 1, 0.3f + 0.5f * onFoot.Heat, 0.1f + 0.3f * onFoot.Heat, 1 };
		embers.EndColor = { 1, 0.05f, 0.02f, 0 };
		embers.MaxParticles = 100;
	}
	else
		Effects::Release(world, onFoot.Embers);

	// Steam hisses out of an overheated gun, weaker as it cools.
	if (onFoot.OverheatLeft > 0)
	{
		const float left = onFoot.OverheatLeft / std::max(config.OverheatSeconds, 0.01f);
		ParticleEmitterComponent& steam = Effects::Hold(world, onFoot.Steam, "Laser Steam", muzzle);
		steam.Emitting = true;
		steam.Rate = 25 + 60 * left;
		steam.Direction = (eye.GetUp() + eye.GetForward() * 0.4f).Normalized();
		steam.Spread = 35;
		steam.Speed = 0.6f + 1.2f * left;
		steam.Gravity = -0.2f;
		steam.Drag = 2;
		steam.Lifetime = 0.7f;
		steam.StartSize = 0.03f;
		steam.EndSize = 0.08f + 0.05f * left;
		steam.StartColor = { 0.85f, 0.88f, 0.9f, 0.4f };
		steam.EndColor = { 0.8f, 0.82f, 0.85f, 0 };
		steam.Blend = "Alpha";
		steam.MaxParticles = 120;
	}
	else
		Effects::Release(world, onFoot.Steam);

	// Shake only moves the camera, never the aim: it builds with the square of the heat, so a
	// tap is steady and a long burst rattles, plus a jolt from each shot.
	if (const Entity camera = FindCamera(world); camera != NullEntity)
	{
		const float trauma = std::min(1.0f, onFoot.Heat * onFoot.Heat + 0.4f * onFoot.Kick);
		const float buzz = onFoot.Time * 28;
		TransformComponent& cameraTransform = world.Get<TransformComponent>(camera);
		cameraTransform.Rotation.X += config.ShakeDegrees * trauma * Wander(buzz, 0) + config.RecoilDegrees * onFoot.Kick;
		cameraTransform.Rotation.Y += config.ShakeDegrees * trauma * Wander(buzz, 11);
		cameraTransform.Rotation.Z += config.ShakeRollDegrees * trauma * Wander(buzz * 0.7f, 23);
		cameraTransform.Position += eye.GetUp() * (0.015f * trauma * Wander(buzz * 1.3f, 37));
	}

	if (onFoot.BeamTimeLeft > 0)
	{
		onFoot.BeamTimeLeft -= deltaTime;
		if (onFoot.BeamTimeLeft <= 0)
			world.Get<TransformComponent>(onFoot.Beam).Scale = {};
	}
}
