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
	FP::Vec3 Axis{ 0, 0, 1 }; // Z spins in 2D, Y turns things in 3D
};

// Moves this entity toward the player in X and Y. Works on cameras, lights or anything else.
struct FollowPlayerComponent
{
	float Smoothing = 5;
};

struct AudioDemoComponent
{
	FP::String Clip = "Audio/Click.wav";
};
