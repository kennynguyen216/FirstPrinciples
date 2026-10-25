#pragma once

#include "Engine/Engine.h"

// The entity showing the title screen page (UI/Title.html), with its ports.
struct TitleScreenComponent
{
	// The port a hosted game listens on. If it is taken, as when two games host on one machine, the
	// system picks a free one and announces that instead.
	FP::int32 GamePort = 7777;
	// Hosts announce themselves on this port to everyone on the LAN, and the title screen listens.
	FP::int32 DiscoveryPort = 7778;
};

// On the local Driver during the countdown: where they are looking around the cab, before the
// Driver's systems start at GO. The launcher hands it to the cab so the view carries straight on.
struct StartLookComponent
{
	float Yaw = 0;
	float Pitch = 0;
};

// What the player picked on the title screen page. The page's TitleScreen script writes it and
// TitleSystem acts on it, since page events reach scripts rather than systems.
struct TitleRequestComponent
{
	FP::String Action; // host, join, start or back
	FP::String Address; // for join: "192.168.1.20:7777"
};
