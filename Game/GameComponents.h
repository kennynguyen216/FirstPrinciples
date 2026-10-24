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

// Moves this entity toward the player in X and Y. Works on cameras, lights or anything else.
struct FollowPlayerComponent
{
	float Smoothing = 5;
};
