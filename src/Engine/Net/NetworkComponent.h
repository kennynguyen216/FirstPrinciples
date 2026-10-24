#pragma once

#include "Core/CoreMinimal.h"
#include "Engine/Math.h"

namespace FP
{
	// Shares an entity with the other peers in a network session. The peer that owns it sends its
	// transform and everyone else follows smoothly.
	//
	// Entities spawned while playing belong to the peer that spawned them and go away when it
	// leaves. Entities from the scene belong to the session's authority (the lowest peer id),
	// which moves on to the next peer when it leaves.
	struct NetworkComponent
	{
		// Each peer replaces this scene entity with its own copy when a session starts. Use it for
		// the player, so everyone gets one and sees everyone else's.
		bool OnePerPeer = false;
		// Transform updates per second from the owner.
		float SendRate = 20;

		// Set by NetworkSystem while playing and never saved.
		// True when another peer owns this entity. Game systems should leave such entities alone.
		bool Remote = false;
		uint64 Owner = 0;
		uint32 LastTick = 0;
		bool HasTarget = false;
		Vec3 TargetPosition;
		Vec3 TargetRotation;
		Vec3 TargetScale{ 1, 1, 1 };
		double NextSend = 0;
	};
}
