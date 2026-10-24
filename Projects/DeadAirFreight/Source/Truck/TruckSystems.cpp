#include "Truck/TruckSystems.h"
#include "LifeSupport/Cargo.h"
#include "Match/MatchQueries.h"

#include <cmath>

using namespace FP;
using namespace MatchQueries;

namespace
{
	// Camera jumps instead of gliding when it is this far from where it should be, such as when a
	// Defender's view first finds their truck.
	constexpr float SnapDistance = 150;
	// How far ahead of the truck the camera looks.
	constexpr float LookAhead = 6;

	// Vents cargo oxygen into the thrusters. Leaking tanks make it cost more. False once the cargo
	// has no oxygen left to vent.
	bool Boost(World& world, int32 crew, float deltaTime)
	{
		const LifeSupportConfigComponent config = Cargo::GetConfig(world);
		const Cargo::Totals cargo = Cargo::Sum(world, crew);
		if (cargo.OxygenKg <= 0)
			return false;
		const float cost = config.BoostOxygenKgPerSecond * (1.0f + config.LeakBoostPenalty * cargo.LeakingTanks) * deltaTime;
		const float vented = Cargo::Draw(world, crew, CargoKind::Oxygen, cost);
		if (TruckResourcesComponent* resources = Cargo::FindResources(world, crew))
			resources->OxygenBoostedKg += vented;
		return vented > 0;
	}
}

void TruckSystem::Update(World& world, float deltaTime)
{
	const MatchStateComponent* state = GetState(world);
	if (!state)
		return;
	const Entity match = FindMatch(world);
	const TruckConfigComponent* configured = world.TryGet<TruckConfigComponent>(match);
	const TruckConfigComponent config = configured ? *configured : TruckConfigComponent{};

	const Entity local = FindLocalMember(world);
	if (local == NullEntity)
		return;
	CrewMemberComponent& member = world.Get<CrewMemberComponent>(local);
	if (member.Slot < 0 || member.Role != CrewRole::Driver)
		return;
	TransformComponent& transform = world.Get<TransformComponent>(local);

	// Docked at the colony.
	if (state->Crews[member.Crew].Arrived)
	{
		member.Velocity = {};
		return;
	}

	TruckResourcesComponent* resources = Cargo::FindResources(world, member.Crew);
	// A crew with no air left passes out and the truck coasts.
	const bool conscious = !resources || !resources->OutOfAir;
	if (state->Phase == MatchPhase::Racing && conscious)
	{
		const Vec2 axis = Input::GetMoveAxis();
		const bool boost = (Input::IsKeyDown(SDL_SCANCODE_LSHIFT) || Input::IsKeyDown(SDL_SCANCODE_RSHIFT)) && Boost(world, member.Crew, deltaTime);
		const float multiplier = boost ? config.BoostMultiplier : 1.0f;

		transform.Rotation.Y = std::fmod(transform.Rotation.Y - axis.X * config.TurnRate * deltaTime, 360.0f);
		member.Velocity += transform.GetForward() * (axis.Y * config.Thrust * multiplier * deltaTime);

		const float topSpeed = config.MaxSpeed * multiplier;
		const float speed = member.Velocity.Length();
		if (speed > topSpeed)
			member.Velocity *= topSpeed / speed;
	}
	else if (state->Phase == MatchPhase::Lobby || state->Phase == MatchPhase::Countdown)
	{
		// Held at the start line until the race begins.
		member.Velocity = {};
		return;
	}

	// Space has no friction, but a little drag keeps the truck steerable. After the race it
	// coasts to a stop.
	member.Velocity *= std::exp(-config.Drag * deltaTime);
	transform.Position += member.Velocity * deltaTime;
}

void ChaseCameraSystem::Update(World& world, float deltaTime)
{
	// The truck to follow is our own. A Defender's camera belongs to DefenderSystem once the
	// match is under way; in the lobby it watches the crew's truck.
	Entity target = FindLocalMember(world);
	if (target != NullEntity)
	{
		const CrewMemberComponent& member = world.Get<CrewMemberComponent>(target);
		if (member.Slot >= 0 && member.Role == CrewRole::Defender)
		{
			const MatchStateComponent* state = GetState(world);
			if (state && state->Phase != MatchPhase::Lobby)
				return;
			const Entity driver = FindDriver(world, member.Crew);
			if (driver != NullEntity)
				target = driver;
		}
	}
	if (target == NullEntity)
		return;

	const TransformComponent& followed = world.Get<TransformComponent>(target);
	const Vec3 forward = followed.GetForward();
	for (auto [entity, transform, chase] : world.View<TransformComponent, ChaseCameraComponent>().each())
	{
		const Vec3 goal = followed.Position - forward * chase.Distance + Vec3{ 0, chase.Height, 0 };
		if (!m_Snapped || (goal - transform.Position).Length() > SnapDistance)
			transform.Position = goal;
		else
			transform.Position += (goal - transform.Position) * (1.0f - std::exp(-chase.Smoothing * deltaTime));

		const Vec3 look = followed.Position + forward * LookAhead - transform.Position;
		const float flat = std::sqrt(look.X * look.X + look.Z * look.Z);
		transform.Rotation = { std::atan2(look.Y, flat) * (180.0f / 3.14159265f), YawToward(transform.Position, transform.Position + look), 0 };
	}
	m_Snapped = true;
}
