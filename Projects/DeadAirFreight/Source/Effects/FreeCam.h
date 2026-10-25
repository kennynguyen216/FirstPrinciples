#pragma once

#include "Engine/Engine.h"

// A camera for filming trailers. F detaches the view and hides the HUD. While flying, WASD moves,
// Space and Left Ctrl rise and sink, Shift is fast, Left Alt is slow, the mouse looks and the wheel
// zooms. G hands control back to the player with the camera held where it is, and again to fly.
// V picks what the held camera keeps still against: riding along with the player (the default,
// which keeps up with the truck) or a fixed spot in space, which the truck flies past.
//
// From outside, the truck is its model, and the cab and the bay behind its hatch wait far away. The
// windshield is a portal that still shows them at full size, so it is bigger on the inside. Fly into
// the hull and the rooms come back. This player's own character is drawn while it is on, in the bay
// or the cab.
//
// It runs last, after everything else that moves the camera. Like the starfield, the driver
// launcher adds a second one after the Driver's systems; they share one camera.
class FreeCamSystem : public FP::System
{
public:
	const char* GetName() const override { return "FreeCam"; }
	void OnStop(FP::World& world) override;
	void Update(FP::World& world, float deltaTime) override;
};

namespace FreeCam
{
	// True while the camera has the controls, so the player's systems should ignore them.
	bool IsFlying();
	bool IsOn();
}
