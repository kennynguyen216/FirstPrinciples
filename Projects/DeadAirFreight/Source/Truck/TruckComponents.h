#pragma once

#include "Engine/Engine.h"

// Handling for every truck, edited on the scene's Match entity.
// Movement is flat on the XZ plane. TruckSystem moves the truck itself as a kinematic body; with
// Jolt it can set the body's velocity instead and let collisions push back.
struct TruckConfigComponent
{
	float Thrust = 30;
	float MaxSpeed = 45;
	// Boost multiplies thrust and top speed. What it costs is in LifeSupportConfig.
	float BoostMultiplier = 2;
	float TurnRate = 80; // degrees per second
	float Drag = 0.5f; // fraction of speed lost per second, roughly
	FP::Vec3 Size{ 3, 2, 6 };
};

// On the camera. Follows the local player's truck, or their crew's truck when they are a Defender.
struct ChaseCameraComponent
{
	float Distance = 18;
	float Height = 6;
	float Smoothing = 6;
};
