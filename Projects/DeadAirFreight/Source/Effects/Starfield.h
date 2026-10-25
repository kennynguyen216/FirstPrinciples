#pragma once

#include "Engine/Engine.h"

// A sphere of stars (a Meshes/Stars mesh) that stays centered on the camera, so the stars sit at
// infinity: they turn with the view but never get nearer, however far the truck flies. The nebulae
// behind them are the scene's skybox (Environments/Space.png). Both come from Tools/MakeSky.py.
struct StarfieldComponent
{
	float Radius = 15000; // inside the camera's far clip, outside everything else
};

// Moves the starfields to the camera. It runs once early for the menus and the bay, and the driver
// launcher adds a second one after the Driver's systems, which move the cab camera with the truck.
class StarfieldSystem : public FP::System
{
public:
	const char* GetName() const override { return "Starfield"; }
	void Update(FP::World& world, float deltaTime) override;
};
