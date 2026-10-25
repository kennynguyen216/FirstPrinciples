#pragma once

#include "Engine/Engine.h"

// Venting the cargo bay, on the Match entity.
struct AirlockConfigComponent
{
	// How long the airlock stays open, and how fast it drags creatures toward it meanwhile.
	float OpenSeconds = 3;
	float PullSpeed = 25;
	// A creature this close to the open airlock is blown out into space.
	float FlushRadius = 2;
	// Cargo oxygen lost every vent, plus this much for each leaking tank.
	float VentOxygenKg = 3;
	float LeakPenaltyKg = 1;
	float CooldownSeconds = 6;
};

// The airlock door in a crew's cargo bay wall.
struct AirlockComponent
{
	FP::int32 Crew = 0;

	// Set while playing and never saved.
	float OpenFor = 0; // seconds it stays open
	float CooldownFor = 0; // seconds until it can open again
};

// The blast door in a crew's bay doorway, placed raised in the scene. It drops shut while the
// airlock is open, sealing the cab off from the bay, and rises again after.
struct BlastDoorComponent
{
	FP::int32 Crew = 0;
	float Drop = 3; // how far down it slides to close
	float CloseSeconds = 0.25f;
	float OpenSeconds = 1.0f;

	// Set while playing and never saved.
	FP::Vec3 Raised;
	float Shut = 0; // 0 up, 1 closed
};

namespace Airlock
{
	// Opens the crew's airlock. False while it is still cooling down from the last vent.
	bool Vent(FP::World& world, FP::int32 crew);
	// True while the crew's airlock is open.
	bool IsOpen(FP::World& world, FP::int32 crew);
	// True while the crew's blast door is anywhere but fully up, so nobody passes the doorway.
	bool IsSealed(FP::World& world, FP::int32 crew);
}

// Drags a crew's creatures out of the open airlock. A flushed creature takes what it stole with it,
// so that air is gone for good, unlike a kill, which returns it.
class AirlockSystem : public FP::System
{
public:
	const char* GetName() const override { return "Airlock"; }
	void OnStart(FP::World& world) override;
	void Update(FP::World& world, float deltaTime) override;
};
