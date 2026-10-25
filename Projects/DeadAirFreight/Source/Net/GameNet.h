#pragma once

#include "Engine/Engine.h"
#include "Engine/Net/NetBuffer.h"
#include "Engine/Net/NetSession.h"

// Each truck runs on the machine of its crew's first player, the captain, who tells everyone where
// it is; a crewmate on another machine sends the captain what they do at the wheel and shows the
// truck the captain sends back. The host runs the rest of the match: the phases, the seats, both
// cargo bays, the creatures, the tanks, and who wins. The host is the machine that hosted the
// session (everyone else joined it), and it tells the others so. Without a network session this
// machine is the host and the captain, so playing alone works the same.
namespace GameNet
{
	enum class Message : FP::uint8
	{
		// Host to everyone.
		HostClaim, // "I am the host", a few times a second
		MatchState, // phase, countdown, race time, winner
		DriverPackage, // the host's weather and asteroids, in pieces
		BayState, // some of one crew's tanks
		BayCreatures, // one crew's creatures and airlock
		DriverResults, // why the match ended
		RaceAgain, // the same players race again: everyone restarts the match
		// Everyone to the host.
		CargoSpend, // oxygen a truck used (breathing, tickets) or won back, out of or into its tanks
		TruckFinished, // a truck delivered, or is out of the race
		// Everyone to everyone.
		TruckPose, // captain: where its truck is, every frame, so the others can see it
		TruckState, // captain: its whole truck, life support and who holds the wheel, for crewmates
		CrewInput, // crewmate: seated, steering and repairing, for their captain
		CrewInCab, // where a player stands in their cab, in cab space, so crewmates see them in their own copy
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
