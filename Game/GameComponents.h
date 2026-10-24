#pragma once

#include "Engine/Engine.h"

// Example components. Replace these with your own.

struct PlayerComponent
{
	float Speed = 6;
};

struct SpinnerComponent
{
	float DegreesPerSecond = 90;
};

// Moves the primary camera toward the player.
struct CameraFollowComponent
{
	float Smoothing = 5;
};
