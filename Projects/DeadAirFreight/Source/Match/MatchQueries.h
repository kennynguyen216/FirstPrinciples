#pragma once

#include "Match/MatchComponents.h"
#include "Engine/Net/NetworkComponent.h"

#include <cmath>

// Lookups shared by the game's systems.
namespace MatchQueries
{
	// Owned by another peer. Game systems read these but leave them alone.
	inline bool IsRemote(FP::World& world, FP::Entity entity)
	{
		const FP::NetworkComponent* network = world.TryGet<FP::NetworkComponent>(entity);
		return network && network->Remote;
	}

	// The peer that owns an entity, or 0 outside a network session.
	inline FP::uint64 GetOwner(FP::World& world, FP::Entity entity)
	{
		const FP::NetworkComponent* network = world.TryGet<FP::NetworkComponent>(entity);
		return network ? network->Owner : 0;
	}

	// The Match entity holding MatchConfigComponent and, while playing, MatchStateComponent.
	inline FP::Entity FindMatch(FP::World& world)
	{
		for (auto [entity, config] : world.View<MatchConfigComponent>().each())
			return entity;
		return FP::NullEntity;
	}

	inline MatchStateComponent* GetState(FP::World& world)
	{
		const FP::Entity match = FindMatch(world);
		return match == FP::NullEntity ? nullptr : world.TryGet<MatchStateComponent>(match);
	}

	// This peer's own player. Prefers one with a seat, since the network template lingers for a
	// frame after the session swaps it for our copy.
	inline FP::Entity FindLocalMember(FP::World& world)
	{
		FP::Entity fallback = FP::NullEntity;
		for (auto [entity, member] : world.View<CrewMemberComponent>().each())
		{
			if (IsRemote(world, entity))
				continue;
			if (member.Slot >= 0)
				return entity;
			fallback = entity;
		}
		return fallback;
	}

	// The crew's Driver, which is also its truck.
	inline FP::Entity FindDriver(FP::World& world, FP::int32 crew)
	{
		for (auto [entity, member] : world.View<CrewMemberComponent>().each())
		{
			if (member.Slot >= 0 && member.Crew == crew && member.Role == CrewRole::Driver)
				return entity;
		}
		return FP::NullEntity;
	}

	// Yaw in degrees that points an entity's forward (-Z) from one point toward another.
	inline float YawToward(FP::Vec3 from, FP::Vec3 to)
	{
		const FP::Vec3 direction = to - from;
		return std::atan2(-direction.X, -direction.Z) * (180.0f / 3.14159265f);
	}
}
