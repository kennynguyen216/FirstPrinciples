#include "Enemies/SiphonSystems.h"
#include "Audio/Sounds.h"
#include "Driver/SpaceWeather.h"
#include "LifeSupport/Cargo.h"
#include "Match/MatchQueries.h"
#include "Net/GameNet.h"

#include <algorithm>
#include <cmath>

using namespace FP;
using namespace MatchQueries;

namespace
{
	// Pooled creatures wait this far under their bay, out of sight. They keep their size, since a
	// body's collider is sized from its scale when physics first sees it.
	constexpr float ParkDepth = 200;
	// Mite size empty, and how much bigger a full belly makes it.
	constexpr float MiteScale = 0.7f;
	constexpr float MiteFullGrowth = 0.4f;
	// Mites perch on a tank's rim, toward the side they came from, high enough to show over the rows.
	constexpr float LatchInset = 0.3f;
	constexpr float LatchHeight = 0.3f;
	constexpr float LatchReach = 0.15f;
	constexpr float SpawnHeight = 1.5f;

	SiphonConfigComponent GetConfig(World& world)
	{
		for (auto [entity, config] : world.View<SiphonConfigComponent>().each())
			return config;
		return {};
	}

	bool FindBay(World& world, int32 crew, Vec3& outPosition, InteriorComponent& outBay)
	{
		return FindInterior(world, crew, "Bay", &outPosition, &outBay) != NullEntity;
	}

	// A point on the bay's walls. Stepping the index by the golden ratio spreads spawns evenly
	// around the walls without repeating, the same way for every crew.
	Vec3 WallPoint(Vec3 bay, const InteriorComponent& info, uint32 index)
	{
		const float fraction = std::fmod(index * 0.618034f, 1.0f);
		const float width = info.HalfWidth * 2;
		const float length = info.HalfLength * 2;
		float along = fraction * 2 * (width + length);
		if (along < width)
			return bay + Vec3{ -info.HalfWidth + along, SpawnHeight, -info.HalfLength };
		along -= width;
		if (along < length)
			return bay + Vec3{ info.HalfWidth, SpawnHeight, -info.HalfLength + along };
		along -= length;
		if (along < width)
			return bay + Vec3{ info.HalfWidth - along, SpawnHeight, info.HalfLength };
		along -= width;
		return bay + Vec3{ -info.HalfWidth, SpawnHeight, info.HalfLength - along };
	}

	void Park(World& world, Entity entity, SiphonComponent& siphon)
	{
		if (siphon.Active && world.Has<AudioSourceComponent>(entity))
			world.GetAudio().StopSource(entity);
		siphon.Active = false;
		siphon.Target = NullEntity;
		TransformComponent& transform = world.Get<TransformComponent>(entity);
		Vec3 bay;
		InteriorComponent info;
		FindBay(world, siphon.Crew, bay, info);
		transform.Position = bay - Vec3{ 0, ParkDepth, 0 };
		transform.Scale = { MiteScale, MiteScale, MiteScale };
		siphon.Size = MiteScale;
	}

	bool IsFeedable(World& world, Entity tankEntity, int32 crew)
	{
		if (!world.IsValid(tankEntity))
			return false;
		const CargoTankComponent* tank = world.TryGet<CargoTankComponent>(tankEntity);
		return tank && tank->Crew == crew && tank->Kind == CargoKind::Oxygen && tank->AmountKg > 0;
	}

	Entity FindNearestTank(World& world, int32 crew, Vec3 from)
	{
		Entity nearest = NullEntity;
		float best = 1e30f;
		for (auto [entity, transform, tank] : world.View<TransformComponent, CargoTankComponent>().each())
		{
			if (tank.Crew != crew || tank.Kind != CargoKind::Oxygen || tank.AmountKg <= 0)
				continue;
			const float distance = (transform.Position - from).LengthSquared();
			if (distance < best)
			{
				best = distance;
				nearest = entity;
			}
		}
		return nearest;
	}
}

namespace Siphons
{
	int32 Spawn(World& world, int32 crew, SiphonKind kind, int32 count, uint32 index)
	{
		Vec3 bay;
		InteriorComponent info;
		if (!FindBay(world, crew, bay, info))
			return 0;
		const SiphonConfigComponent config = GetConfig(world);
		int32 woken = 0;
		for (auto [entity, transform, siphon] : world.View<TransformComponent, SiphonComponent>().each())
		{
			if (woken >= count)
				break;
			if (siphon.Active || siphon.Crew != crew || siphon.Kind != kind)
				continue;
			const int32 poolIndex = siphon.Index;
			const uint8 hits = siphon.Hits;
			const uint8 pops = siphon.Pops;
			siphon = SiphonComponent{};
			siphon.Crew = crew;
			siphon.Index = poolIndex;
			siphon.Hits = hits;
			siphon.Pops = pops;
			siphon.Size = MiteScale;
			siphon.Kind = kind;
			siphon.Active = true;
			siphon.Health = config.MiteHealth;
			transform.Position = WallPoint(bay, info, index + woken);
			transform.Scale = { MiteScale, MiteScale, MiteScale };
			if (world.Has<AudioSourceComponent>(entity))
				world.GetAudio().Play(entity);
			if (woken == 0)
				Sounds::PlayAt(world, SoundCue::MiteSpawn, transform.Position);
			++woken;
		}
		return woken;
	}

	void Kill(World& world, Entity entity)
	{
		SiphonComponent& siphon = world.Get<SiphonComponent>(entity);
		if (!siphon.Active)
			return;
		Sounds::PlayAt(world, SoundCue::MiteDeath, world.Get<TransformComponent>(entity).Position);
		++siphon.Pops;
		const float returned = Cargo::Refill(world, siphon.Crew, CargoKind::Oxygen, siphon.StolenOxygenKg);
		if (TruckResourcesComponent* resources = Cargo::FindResources(world, siphon.Crew))
		{
			resources->OxygenRecoveredKg += returned;
			++resources->SiphonsKilled;
		}
		Park(world, entity, siphon);
	}

	void Flush(World& world, Entity entity)
	{
		SiphonComponent& siphon = world.Get<SiphonComponent>(entity);
		if (!siphon.Active)
			return;
		if (TruckResourcesComponent* resources = Cargo::FindResources(world, siphon.Crew))
		{
			resources->OxygenFlushedKg += siphon.StolenOxygenKg;
			++resources->SiphonsFlushed;
		}
		Park(world, entity, siphon);
	}

	int32 CountActive(World& world, int32 crew)
	{
		int32 count = 0;
		for (auto [entity, siphon] : world.View<SiphonComponent>().each())
			count += siphon.Active && siphon.Crew == crew ? 1 : 0;
		return count;
	}
}

void SiphonSpawnerSystem::OnStart(World& world)
{
	m_SinceTrickle = 0;
	m_NextIndex = 0;
	m_Random = 1;
	const SiphonConfigComponent config = GetConfig(world);
	m_NextTrickle = config.MiteTrickleSeconds;
	const String chitter = Sounds::GetClip(world, SoundCue::MiteChitter);
	for (int32 crew = 0; crew < CrewCount; ++crew)
	{
		for (int32 i = 0; i < config.MitePoolPerCrew; ++i)
		{
			const Entity entity = world.CreateEntity(String::Format("Crew %s Mite %d", GetCrewName(crew), i + 1));
			SiphonComponent& siphon = world.Add<SiphonComponent>(entity);
			siphon.Crew = crew;
			siphon.Index = i;
			siphon.Kind = SiphonKind::Mite;
			world.Add<MeshComponent>(entity).Mesh = "Sphere";
			world.Add<MaterialComponent>(entity).Material = "Materials/Mite.material";
			// A kinematic sphere: it follows the Mite's transform and the laser can hit it.
			world.Add<ColliderComponent>(entity).Shape = "Sphere";
			world.Add<RigidBodyComponent>(entity).Motion = "Kinematic";
			if (!chitter.IsEmpty())
			{
				// A positioned loop, so the Defender can hear where Mites are before seeing them.
				AudioSourceComponent& source = world.Add<AudioSourceComponent>(entity);
				source.Clip = chitter;
				source.Loop = true;
				source.Spatial = true;
				source.MaxDistance = 15;
			}
			Park(world, entity, siphon);
		}
	}
}

void SiphonSpawnerSystem::Update(World& world, float deltaTime)
{
	const MatchStateComponent* state = GetState(world);
	if (!state || state->Phase != MatchPhase::Racing || !GameNet::IsHost(world))
		return;
	const SiphonConfigComponent config = GetConfig(world);
	if (config.MiteTrickleSeconds <= 0)
		return;
	m_SinceTrickle += deltaTime;
	while (m_SinceTrickle >= m_NextTrickle)
	{
		m_SinceTrickle -= m_NextTrickle;
		const float spread = std::max(config.MiteTrickleMaxSeconds - config.MiteTrickleSeconds, 0.0f);
		m_NextTrickle = std::max(config.MiteTrickleSeconds + spread * NextRandom(m_Random), 0.1f);
		// The same spot in both bays keeps the crews' bays fair.
		for (int32 crew = 0; crew < CrewCount; ++crew)
			Siphons::Spawn(world, crew, SiphonKind::Mite, 1, m_NextIndex);
		++m_NextIndex;
	}
}

void SiphonSystem::Update(World& world, float deltaTime)
{
	const MatchStateComponent* state = GetState(world);
	if (!state || state->Phase != MatchPhase::Racing || !GameNet::IsHost(world))
		return;
	const SiphonConfigComponent config = GetConfig(world);

	for (auto [entity, transform, siphon] : world.View<TransformComponent, SiphonComponent>().each())
	{
		if (!siphon.Active)
			continue;
		siphon.Age += deltaTime;

		if (!IsFeedable(world, siphon.Target, siphon.Crew))
		{
			siphon.Target = FindNearestTank(world, siphon.Crew, transform.Position);
			siphon.LatchedSeconds = 0;
			siphon.Bitten = false;
		}
		if (siphon.Target == NullEntity)
			continue; // nothing left to eat

		// Perch on the tank's rim, on the side the Mite came from.
		const TransformComponent& tankTransform = world.Get<TransformComponent>(siphon.Target);
		Vec3 side = transform.Position - tankTransform.Position;
		side.Y = 0;
		side = side.LengthSquared() > 1e-6f ? side.Normalized() : Vec3{ 0, 0, 1 };
		const float tankRadius = std::max(tankTransform.Scale.X, tankTransform.Scale.Z) * 0.5f;
		const Vec3 latch = tankTransform.Position + side * std::max(0.0f, tankRadius - LatchInset) + Vec3{ 0, tankTransform.Scale.Y * 0.5f + LatchHeight, 0 };

		const Vec3 toLatch = latch - transform.Position;
		const float distance = toLatch.Length();
		if (distance > LatchReach)
		{
			const float step = std::min(distance, config.MiteSpeed * deltaTime);
			transform.Position += toLatch * (step / distance);
			siphon.LatchedSeconds = 0;
		}
		else
		{
			CargoTankComponent& tank = world.Get<CargoTankComponent>(siphon.Target);
			const float eaten = std::min({ config.MiteDrainKgPerSecond * deltaTime, tank.AmountKg, std::max(0.0f, config.MiteBellyKg - siphon.StolenOxygenKg) });
			tank.AmountKg -= eaten;
			siphon.StolenOxygenKg += eaten;
			if (TruckResourcesComponent* resources = Cargo::FindResources(world, siphon.Crew))
				resources->OxygenEatenKg += eaten;

			siphon.LatchedSeconds += deltaTime;
			if (config.MiteBiteHoleSeconds > 0 && !siphon.Bitten && siphon.LatchedSeconds >= config.MiteBiteHoleSeconds)
			{
				++tank.Holes;
				// The hole is in the dome under the Mite, where the air will hiss out.
				const float radius = tankTransform.Scale.X * 0.5f;
				const float out = std::max(0.0f, radius - LatchInset);
				const float shoulder = tankTransform.Scale.Y * 0.5f - radius;
				tank.LeakPoint = tankTransform.Position + side * out + Vec3{ 0, shoulder + std::sqrt(std::max(0.0f, radius * radius - out * out)), 0 };
				siphon.Bitten = true;
				Sounds::PlayAt(world, SoundCue::MiteBite, transform.Position);
			}
		}

		// A Mite swells as it feeds, so a fat one is worth shooting first, and again with every hit
		// until it pops. Every machine animates the jiggle on top (CombatEffectsSystem).
		const float fullness = config.MiteBellyKg > 0 ? siphon.StolenOxygenKg / config.MiteBellyKg : 0;
		const float hurt = config.MiteHealth > 0 ? std::clamp(1 - siphon.Health / config.MiteHealth, 0.0f, 1.0f) : 0;
		siphon.Size = (MiteScale + MiteFullGrowth * fullness) * (1 + config.MiteHitSwell * hurt);
	}
}
