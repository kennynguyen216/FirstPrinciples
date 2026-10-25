#pragma once

#include "Engine/Engine.h"
#include "Engine/Net/NetBuffer.h"
#include "Engine/Net/NetSession.h"

// The host runs the whole match: the trucks, the bays, the creatures and the cargo. Everyone else
// sends what they do to the host and shows what the host sends back. The host is the machine that
// hosted the session (everyone else joined it), and it tells the others so. Without a network
// session this machine is the host, so playing alone works the same.
namespace GameNet
{
	enum class Message : FP::uint8
	{
		// Host to everyone.
		HostClaim, // "I am the host", a few times a second
		MatchState, // phase, countdown, race time, winner
		DriverPackage, // the host's weather and asteroids, in pieces
		TruckState, // the truck, its life support and who holds the wheel, every frame
		BayState, // one crew's tanks, creatures and airlock
		DriverResults, // why the match ended
		// Everyone to the host.
		CrewInput, // seated, steering and repairing, every frame
		// Everyone to everyone.
		CrewInCab, // where a player stands in the cab, in cab space, so crewmates see them in their own copy
		LaserHit, // vaporize: a creature hit or a tank punctured
		LaserRepair, // repair beam time on a tank
		Vent, // vent a crew's bay
	};

	bool IsNetworked(FP::World& world);
	// True on the host, and always when not networked.
	bool IsHost(FP::World& world);
	FP::PeerId GetLocalId(FP::World& world);
	// The host's peer id, or 0 until its claim arrives.
	FP::PeerId GetHostId(FP::World& world);
	// Called when a host's claim arrives.
	void SetHostId(FP::PeerId host);

	// Starts a message of a kind; write the rest and Send it.
	FP::NetWriter Begin(Message message);
	// to 0 sends to everyone else. Nothing happens when not networked.
	void Send(FP::World& world, const FP::NetWriter& message, FP::NetChannel channel, FP::PeerId to = 0);
	// Sends to the host. The host itself never needs to.
	void SendToHost(FP::World& world, const FP::NetWriter& message, FP::NetChannel channel);

	// The next game message from another peer.
	bool Poll(FP::World& world, FP::NetEvent& out);
}
