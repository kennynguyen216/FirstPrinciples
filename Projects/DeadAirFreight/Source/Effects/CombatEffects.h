#pragma once

#include "Engine/Engine.h"
#include "Engine/Material.h"

#include <unordered_map>
#include <vector>

// What a mining laser shot landed on, for the look of its impact.
enum class ImpactKind : FP::uint8
{
	Nothing, // out of range
	Wall,
	Tank,
	Mite,
};

// Particle effects for the fight in the bay. Every one is built in code and animated as it plays:
// counts, speeds, colors and sizes come from what caused it (how hot the laser is, how fat the Mite
// was, how much air is left in a tank), with a little randomness so no two look alike.
namespace Effects
{
	// One mining laser shot, seen by the shooter: a flash at the gun, and sparks and smoke where it
	// lands. Heat (0 to 1) is how long the trigger has been held; hot shots throw more and brighter.
	void LaserShot(FP::World& world, FP::Vec3 muzzle, FP::Vec3 forward, FP::Vec3 point, FP::Vec3 normal, ImpactKind kind, float heat);

	// Keeps a held emitter going at a point this frame, making it the first time. Pass rate 0 to let
	// it fade out. These animate every frame from their callers.
	FP::ParticleEmitterComponent& Hold(FP::World& world, FP::Entity& emitter, const char* name, FP::Vec3 at);
	void Release(FP::World& world, FP::Entity& emitter);
}

// Runs on every machine, from what the host sends: Mites jiggle when hit and swell like water
// balloons, then pop in a spray of alien blood; holed tanks pulse red and hiss air (or spit water)
// from the hole.
class CombatEffectsSystem : public FP::System
{
public:
	const char* GetName() const override { return "Combat Effects"; }
	void OnStart(FP::World& world) override;
	void Update(FP::World& world, float deltaTime) override;
	void OnStop(FP::World& world) override;

private:
	struct MiteLook
	{
		FP::uint8 Hits = 0;
		FP::uint8 Pops = 0;
		bool Active = false;
		float Jiggle = 0; // squash (+) or stretch (-) of the body
		float JiggleSpeed = 0;
		float Phase = 0; // so Mites do not all breathe together
		FP::Vec3 Position; // where it was last frame, for the pop after it is gone
		float Size = 0;
	};

	// The thin skin of a popped Mite, flashing outward for a moment.
	struct Shell
	{
		FP::Entity Entity = FP::NullEntity;
		float Age = 0;
		float Size = 0;
	};

	void UpdateMites(FP::World& world, float deltaTime);
	void UpdateLeaks(FP::World& world);
	void UpdatePulse();
	void Pop(FP::World& world, FP::Vec3 at, float size);

	float m_Time = 0;
	std::unordered_map<FP::Entity, MiteLook> m_Mites;
	std::unordered_map<FP::Entity, FP::Entity> m_Leaks; // tank to its leak's emitter
	std::vector<Shell> m_Shells;
	// The leaking tank material, animated while playing and put back after.
	FP::Material* m_Pulse = nullptr;
	FP::MaterialData m_PulseRest;
};
