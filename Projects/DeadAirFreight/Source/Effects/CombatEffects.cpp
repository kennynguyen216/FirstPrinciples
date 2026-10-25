#include "Effects/CombatEffects.h"
#include "Enemies/SiphonComponents.h"
#include "LifeSupport/LifeSupportComponents.h"

#include <algorithm>
#include <cmath>

using namespace FP;

namespace
{
	const char* const LeakingMaterial = "Materials/TankLeaking.material";
	const char* const MiteMaterial = "Materials/Mite.material";

	// Alien blood: a sick lime green that reads against the purple Mites and the blue bay.
	const Color BloodBright{ 0.55f, 1.0f, 0.15f, 1 };
	const Color BloodDark{ 0.15f, 0.45f, 0.02f, 0 };

	// Mite jiggle: a stiff spring, lightly damped, so a hit wobbles a few times and settles.
	constexpr float JiggleStiffness = 260;
	constexpr float JiggleDamping = 9;
	constexpr float JiggleKick = 6; // per hit
	constexpr float JiggleLimit = 0.45f;
	constexpr float BreathDepth = 0.05f;
	constexpr float BreathRate = 12;

	// The popped skin flashes out this far, this fast.
	constexpr float ShellSeconds = 0.08f;
	constexpr float ShellGrowth = 0.5f;

	// Holed tanks pulse between these, this many times a second.
	constexpr float PulseRate = 1.4f;
	const Color PulseDim{ 0.3f, 0.04f, 0.04f, 1 };
	const Color PulseBright{ 1.0f, 0.12f, 0.08f, 1 };
	constexpr float PulseGlowDim = 0.2f;
	constexpr float PulseGlowBright = 3.5f;

	Color Tint(Color color, float alpha)
	{
		color.A = alpha;
		return color;
	}

	// A burst of particles at a point that cleans itself up once the last one is gone.
	ParticleEmitterComponent& Burst(World& world, Vec3 at, int32 count)
	{
		const Entity entity = world.CreateEntity("Effect");
		world.Get<TransformComponent>(entity).Position = at;
		ParticleEmitterComponent& emitter = world.Add<ParticleEmitterComponent>(entity);
		emitter.Emitting = false;
		emitter.Rate = 0;
		emitter.PendingBurst = count;
		emitter.MaxParticles = std::max(count, 1);
		emitter.DestroyWhenDone = true;
		return emitter;
	}

	// Smooth wandering in -1 to 1, from a few sines that never line up.
	float Wander(float t, float seed)
	{
		return (std::sin(t + seed) + 0.6f * std::sin(t * 2.3f + seed * 1.7f) + 0.3f * std::sin(t * 4.7f + seed * 0.3f)) / 1.9f;
	}

	// Which way the hole faces: out of the side wall, or up and out of the dome.
	Vec3 LeakDirection(const TransformComponent& tank, Vec3 leak)
	{
		Vec3 out = leak - tank.Position;
		const float radius = tank.Scale.X * 0.5f;
		const float shoulder = tank.Scale.Y * 0.5f - radius;
		const float up = std::max(0.0f, out.Y - shoulder) / std::max(radius, 0.01f);
		out.Y = 0;
		out = out.LengthSquared() > 1e-6f ? out.Normalized() : Vec3{ 0, 0, 1 };
		return (out + Vec3{ 0, up * 1.5f, 0 }).Normalized();
	}
}

namespace Effects
{
	void LaserShot(World& world, Vec3 muzzle, Vec3 forward, Vec3 point, Vec3 normal, ImpactKind kind, float heat)
	{
		heat = Clamp01(heat);

		// The gun: a red snap along the beam.
		ParticleEmitterComponent& flash = Burst(world, muzzle + forward * 0.1f, 4 + static_cast<int32>(4 * heat));
		flash.Direction = forward;
		flash.Spread = 25;
		flash.Speed = Random::Range(1.5f, 3.0f);
		flash.Lifetime = 0.07f;
		flash.StartSize = 0.05f + 0.04f * heat;
		flash.EndSize = 0;
		flash.StartColor = { 1, 0.35f + 0.4f * heat, 0.2f, 1 };
		flash.EndColor = { 1, 0.05f, 0.02f, 0 };

		if (kind == ImpactKind::Nothing)
			return;

		// A flash of light where it lands, bigger the hotter the gun.
		ParticleEmitterComponent& glow = Burst(world, point + normal * 0.05f, 2);
		glow.Speed = 0;
		glow.Lifetime = 0.08f;
		glow.LifetimeRandom = 0;
		glow.StartSize = Random::Range(0.35f, 0.5f) * (1 + heat);
		glow.EndSize = 0;
		glow.StartColor = { 1, 0.25f + 0.35f * heat, 0.15f, 0.9f };
		glow.EndColor = { 1, 0.05f, 0.02f, 0 };

		if (kind == ImpactKind::Mite)
			return; // the Mite's own splatter comes from CombatEffectsSystem, on every machine

		// Sparks off metal, spraying back out of the surface and falling.
		const int32 sparks = 10 + static_cast<int32>(16 * heat) + static_cast<int32>(Random::Range(0.0f, 5.0f));
		ParticleEmitterComponent& spray = Burst(world, point + normal * 0.03f, sparks);
		spray.Direction = normal;
		spray.Spread = Random::Range(55.0f, 75.0f);
		spray.Speed = Random::Range(4.0f, 6.5f) * (1 + 0.5f * heat);
		spray.SpeedRandom = 0.7f;
		spray.Gravity = 0.8f;
		spray.Drag = 1.8f;
		spray.Lifetime = 0.3f + 0.2f * heat;
		spray.LifetimeRandom = 0.6f;
		spray.StartSize = 0.09f + 0.05f * heat;
		spray.EndSize = 0;
		spray.StartColor = { 1, 0.5f + 0.4f * heat, 0.25f + 0.4f * heat, 1 };
		spray.EndColor = { 1, 0.1f, 0.03f, 0 };

		// A wisp of scorched smoke that curls up.
		ParticleEmitterComponent& smoke = Burst(world, point + normal * 0.08f, 2 + static_cast<int32>(3 * heat));
		smoke.Direction = (normal + Vec3{ 0, 1, 0 }).Normalized();
		smoke.Spread = 40;
		smoke.Speed = 0.4f;
		smoke.Gravity = -0.15f;
		smoke.Drag = 1.2f;
		smoke.Lifetime = 0.9f;
		smoke.StartSize = 0.1f;
		smoke.EndSize = 0.5f + 0.3f * heat;
		smoke.StartColor = { 0.3f, 0.3f, 0.32f, 0.35f };
		smoke.EndColor = { 0.2f, 0.2f, 0.22f, 0 };
		smoke.Blend = "Alpha";

		if (kind == ImpactKind::Tank)
		{
			// A shot into cargo: a gasp of escaping air.
			ParticleEmitterComponent& gasp = Burst(world, point + normal * 0.05f, 14);
			gasp.Direction = normal;
			gasp.Spread = 35;
			gasp.Speed = Random::Range(2.5f, 4.0f);
			gasp.Drag = 3;
			gasp.Lifetime = 0.5f;
			gasp.StartSize = 0.08f;
			gasp.EndSize = 0.5f;
			gasp.StartColor = { 0.9f, 0.95f, 1, 0.5f };
			gasp.EndColor = { 0.85f, 0.9f, 1, 0 };
			gasp.Blend = "Alpha";
		}
	}

	ParticleEmitterComponent& Hold(World& world, Entity& emitter, const char* name, Vec3 at)
	{
		if (!world.IsValid(emitter) || !world.Has<ParticleEmitterComponent>(emitter))
		{
			emitter = world.CreateEntity(name);
			world.Add<ParticleEmitterComponent>(emitter).Emitting = true;
		}
		world.Get<TransformComponent>(emitter).Position = at;
		ParticleEmitterComponent& held = world.Get<ParticleEmitterComponent>(emitter);
		held.DestroyWhenDone = false;
		return held;
	}

	void Release(World& world, Entity& emitter)
	{
		if (world.IsValid(emitter))
		{
			if (ParticleEmitterComponent* held = world.TryGet<ParticleEmitterComponent>(emitter))
			{
				// Let what is in the air finish, then go.
				held->Emitting = false;
				held->DestroyWhenDone = true;
			}
		}
		emitter = NullEntity;
	}
}

void CombatEffectsSystem::OnStart(World& world)
{
	m_Time = 0;
	m_Mites.clear();
	m_Leaks.clear();
	m_Shells.clear();
	m_Pulse = MaterialLibrary::Get().Load(LeakingMaterial);
	if (m_Pulse)
		m_PulseRest = m_Pulse->GetData();
}

void CombatEffectsSystem::OnStop(World& world)
{
	// The material is a shared asset, so the editor gets it back as it was saved.
	if (m_Pulse)
		m_Pulse->GetData() = m_PulseRest;
	m_Pulse = nullptr;
	m_Mites.clear();
	m_Leaks.clear();
	m_Shells.clear();
}

void CombatEffectsSystem::Update(World& world, float deltaTime)
{
	m_Time += deltaTime;
	UpdateMites(world, deltaTime);
	UpdateLeaks(world);
	UpdatePulse();
}

void CombatEffectsSystem::UpdateMites(World& world, float deltaTime)
{
	// Long frames would make the spring blow up, so it steps in small pieces.
	const int32 steps = std::clamp(static_cast<int32>(std::ceil(deltaTime / (1.0f / 120))), 1, 8);
	const float step = deltaTime / steps;

	for (auto [entity, transform, siphon] : world.View<TransformComponent, SiphonComponent>().each())
	{
		auto [found, first] = m_Mites.try_emplace(entity);
		MiteLook& look = found->second;
		if (first)
		{
			look.Hits = siphon.Hits;
			look.Pops = siphon.Pops;
			look.Phase = Random::Range(0.0f, 2 * Pi);
		}

		if (siphon.Pops != look.Pops)
		{
			// Popped since last frame. It is already parked out of sight, so it bursts where it was.
			if (look.Active)
				Pop(world, look.Position, look.Size);
			look.Pops = siphon.Pops;
			look.Jiggle = look.JiggleSpeed = 0;
		}
		if (siphon.Hits != look.Hits)
		{
			const int32 hits = static_cast<uint8>(siphon.Hits - look.Hits);
			look.Hits = siphon.Hits;
			if (siphon.Active)
			{
				look.JiggleSpeed += JiggleKick * hits;
				// A splat of blood from the wound, more as it fills up.
				ParticleEmitterComponent& splat = Burst(world, transform.Position, 6 + 3 * hits);
				splat.Shape = "Sphere";
				splat.ShapeSize = { siphon.Size * 0.4f, 0, 0 };
				splat.Spread = 180;
				splat.Speed = Random::Range(1.5f, 3.0f);
				splat.Gravity = 1.2f;
				splat.Drag = 1;
				splat.Lifetime = 0.45f;
				splat.StartSize = 0.07f;
				splat.EndSize = 0.03f;
				splat.StartColor = BloodBright;
				splat.EndColor = BloodDark;
				splat.Blend = "Alpha";
			}
		}

		look.Active = siphon.Active;
		if (!siphon.Active)
			continue;

		for (int32 i = 0; i < steps; ++i)
		{
			const float pull = -JiggleStiffness * look.Jiggle - JiggleDamping * look.JiggleSpeed;
			look.JiggleSpeed += pull * step;
			look.Jiggle = std::clamp(look.Jiggle + look.JiggleSpeed * step, -JiggleLimit, JiggleLimit);
		}

		// Squash and stretch keep the volume about the same: flatter is wider. A slow breath on
		// top, quicker and deeper the more swollen it is, so a nearly popped Mite trembles.
		const float swollen = std::max(0.0f, siphon.Size / 0.7f - 1);
		const float breath = (BreathDepth + 0.04f * swollen) * std::sin(m_Time * (BreathRate + 10 * swollen) + look.Phase);
		const float wide = siphon.Size * (1 + breath + 0.5f * look.Jiggle);
		const float tall = siphon.Size * (1 + breath - look.Jiggle);
		transform.Scale = { wide, tall, wide };
		look.Position = transform.Position;
		look.Size = siphon.Size;
	}

	for (size_t i = 0; i < m_Shells.size();)
	{
		Shell& shell = m_Shells[i];
		shell.Age += deltaTime;
		if (shell.Age >= ShellSeconds || !world.IsValid(shell.Entity))
		{
			if (world.IsValid(shell.Entity))
				world.DestroyEntity(shell.Entity);
			m_Shells[i] = m_Shells.back();
			m_Shells.pop_back();
			continue;
		}
		const float t = shell.Age / ShellSeconds;
		const float size = shell.Size * (1 + ShellGrowth * std::sqrt(t)) * (1 - t * t);
		world.Get<TransformComponent>(shell.Entity).Scale = { size, size, size };
		++i;
	}
}

void CombatEffectsSystem::Pop(World& world, Vec3 at, float size)
{
	// The skin bursts outward for an instant.
	Shell shell;
	shell.Entity = world.CreateEntity("Mite Skin");
	shell.Size = size;
	TransformComponent& transform = world.Get<TransformComponent>(shell.Entity);
	transform.Position = at;
	transform.Scale = { size, size, size };
	world.Add<MeshComponent>(shell.Entity).Mesh = "Sphere";
	world.Add<MaterialComponent>(shell.Entity).Material = MiteMaterial;
	m_Shells.push_back(shell);

	// Everything scales with the Mite: a fat one makes a mess.
	const float mess = std::clamp(size / 0.7f, 0.8f, 2.5f);

	// The blood, flung every way and raining down.
	ParticleEmitterComponent& blood = Burst(world, at, static_cast<int32>(36 * mess));
	blood.Shape = "Sphere";
	blood.ShapeSize = { size * 0.35f, 0, 0 };
	blood.Spread = 180;
	blood.Speed = Random::Range(4.5f, 6.5f) * std::sqrt(mess);
	blood.SpeedRandom = 0.75f;
	blood.Gravity = 1.3f;
	blood.Drag = 1.4f;
	blood.Lifetime = 0.75f;
	blood.LifetimeRandom = 0.5f;
	blood.StartSize = 0.11f;
	blood.EndSize = 0.04f;
	blood.StartColor = BloodBright;
	blood.EndColor = BloodDark;
	blood.Blend = "Alpha";

	// Heavy gobs thrown up first, falling fast.
	ParticleEmitterComponent& gobs = Burst(world, at, static_cast<int32>(8 * mess));
	gobs.Direction = { 0, 1, 0 };
	gobs.Spread = 60;
	gobs.Speed = Random::Range(3.0f, 4.5f);
	gobs.Gravity = 2.2f;
	gobs.Lifetime = 0.9f;
	gobs.StartSize = 0.16f;
	gobs.EndSize = 0.08f;
	gobs.StartColor = Tint(BloodBright, 0.95f);
	gobs.EndColor = Tint(BloodDark, 0.6f);
	gobs.Blend = "Alpha";

	// A glowing green mist where it was.
	ParticleEmitterComponent& mist = Burst(world, at, 12);
	mist.Shape = "Sphere";
	mist.ShapeSize = { size * 0.3f, 0, 0 };
	mist.Spread = 180;
	mist.Speed = 1.2f;
	mist.Drag = 3;
	mist.Lifetime = 0.45f;
	mist.StartSize = size * 0.5f;
	mist.EndSize = size * 1.2f;
	mist.StartColor = { 0.4f, 1, 0.2f, 0.35f };
	mist.EndColor = { 0.2f, 0.6f, 0.1f, 0 };

	// And the air it stole, puffing back out.
	ParticleEmitterComponent& air = Burst(world, at, static_cast<int32>(10 * mess));
	air.Spread = 180;
	air.Speed = 1.5f;
	air.Drag = 2.5f;
	air.Gravity = -0.1f;
	air.Lifetime = 0.7f;
	air.StartSize = 0.1f;
	air.EndSize = 0.45f;
	air.StartColor = { 0.9f, 0.95f, 1, 0.35f };
	air.EndColor = { 0.9f, 0.95f, 1, 0 };
	air.Blend = "Alpha";
}

void CombatEffectsSystem::UpdateLeaks(World& world)
{
	for (auto [entity, transform, tank] : world.View<TransformComponent, CargoTankComponent>().each())
	{
		const bool leaking = tank.Holes > 0 && tank.AmountKg > 0;
		auto found = m_Leaks.find(entity);
		if (!leaking)
		{
			if (found != m_Leaks.end())
			{
				Effects::Release(world, found->second);
				m_Leaks.erase(found);
			}
			continue;
		}

		Entity& held = m_Leaks[entity];
		const Vec3 leak = tank.LeakPoint.LengthSquared() > 0 ? tank.LeakPoint : transform.Position + Vec3{ 0, transform.Scale.Y * 0.5f, 0 };
		const Vec3 out = LeakDirection(transform, leak);
		ParticleEmitterComponent& jet = Effects::Hold(world, held, "Leak", leak + out * 0.05f);

		// Pressure falls as the tank empties, so the jet weakens. It sputters as it goes, and its
		// aim wanders a little, the way gas through a ragged hole does.
		const float pressure = tank.CapacityKg > 0 ? Clamp01(tank.AmountKg / tank.CapacityKg) : 0;
		const float seed = static_cast<float>(tank.Index) * 1.37f + static_cast<float>(tank.Crew) * 5.1f;
		const float sputter = 0.65f + 0.35f * Wander(m_Time * 9, seed);
		const Vec3 wobble{ Wander(m_Time * 3, seed + 2) * 0.15f, Wander(m_Time * 3.4f, seed + 4) * 0.15f, Wander(m_Time * 2.7f, seed + 6) * 0.15f };
		jet.Direction = (out + wobble).Normalized();
		jet.MaxParticles = 200;
		const float holes = static_cast<float>(std::min(tank.Holes, 5));
		if (tank.Kind == CargoKind::Oxygen)
		{
			jet.Rate = (40 + 25 * holes) * (0.3f + 0.7f * pressure) * sputter;
			jet.Speed = (2.0f + 4.0f * pressure) * (0.8f + 0.3f * sputter);
			jet.Spread = 10 + 12 * (1 - sputter);
			jet.Drag = 2.2f;
			jet.Gravity = -0.05f;
			jet.Lifetime = 0.8f;
			jet.StartSize = 0.1f;
			jet.EndSize = 0.7f + 0.4f * pressure;
			jet.StartColor = { 0.9f, 0.95f, 1, 0.75f * (0.5f + 0.5f * pressure) };
			jet.EndColor = { 0.8f, 0.9f, 1, 0 };
		}
		else
		{
			jet.Rate = (20 + 15 * holes) * (0.3f + 0.7f * pressure) * sputter;
			jet.Speed = (1.2f + 1.8f * pressure) * (0.8f + 0.3f * sputter);
			jet.Spread = 8 + 10 * (1 - sputter);
			jet.Drag = 0.3f;
			jet.Gravity = 1;
			jet.Lifetime = 0.8f;
			jet.StartSize = 0.1f;
			jet.EndSize = 0.06f;
			jet.StartColor = { 0.35f, 0.6f, 1, 0.9f };
			jet.EndColor = { 0.3f, 0.5f, 1, 0.2f };
		}
		jet.Blend = "Alpha";
		jet.Emitting = true;
	}

	// Tanks gone since: let their leaks finish.
	for (auto it = m_Leaks.begin(); it != m_Leaks.end();)
	{
		if (!world.IsValid(it->first))
		{
			Effects::Release(world, it->second);
			it = m_Leaks.erase(it);
		}
		else
			++it;
	}
}

void CombatEffectsSystem::UpdatePulse()
{
	if (!m_Pulse)
		return;
	// A soft heartbeat: up and down, eased so it lingers at the ends.
	float beat = 0.5f - 0.5f * std::cos(m_Time * PulseRate * 2 * Pi);
	beat = beat * beat * (3 - 2 * beat);
	MaterialData& data = m_Pulse->GetData();
	data.BaseColor = Lerp(PulseDim, PulseBright, beat);
	data.EmissiveColor = PulseBright;
	data.EmissiveIntensity = Lerp(PulseGlowDim, PulseGlowBright, beat);
}
