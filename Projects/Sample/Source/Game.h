#pragma once

#include "Engine/Engine.h"

// Entry points the Editor and Runtime call into. This is where the game plugs into the engine.
namespace Game
{
	// Register game components so the editor can show them and scenes can save them.
	void RegisterComponents();

	// Add the game's systems to a world. Called before the world starts playing.
	void AddSystems(FP::World& world);
}
