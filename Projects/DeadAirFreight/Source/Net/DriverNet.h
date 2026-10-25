#pragma once

#include "Driver/DriverSystem.h"
#include "Engine/Net/NetSession.h"
#include "Match/MatchComponents.h"

#include <string>
#include <vector>

// What the network and the driver bridge share about the trucks, on the Match entity while playing.
struct DriverNetComponent
{
	// This machine's Driver systems, for this player's own crew's truck, once launched.
	DriverSystem* Driver = nullptr;

	// Away from the host: the host's match package, which arrives in pieces before the cab can start.
	// Everyone flies through the same weather and asteroids.
	std::string PackageText;
	FP::int32 PackagePieces = 0;
	FP::int32 PackageTotal = -1;
	bool PackageReady = false;
	MatchPackage Package;

	// Why the match ended, waiting to be shown.
	std::string Results;

	// Every truck as its captain last sent it. Rival trucks are drawn from these, and collide with them.
	struct Pose
	{
		bool Heard = false;
		FP::Vec3 Position;
		FP::Vec3 Velocity; // to carry the truck on between messages
		FP::Vec3 Rotation; // pitch, yaw, bank in degrees
		double Time = 0; // world time it arrived
	};
	Pose Trucks[CrewCount];

	// The whole truck from its captain, for crewmates on other machines, who show it as sent.
	struct Truck
	{
		bool Heard = false;
		TruckState State;
		LifeSupport Life;
		FP::PeerId WheelHolder = 0; // the peer at the wheel, or 0 when nobody is
	};
	Truck Crews[CrewCount];

	// On a captain: crewmates on other machines and what they did last.
	struct CrewInput
	{
		FP::PeerId Peer = 0;
		FP::int32 Crew = 0;
		bool Seated = false;
		bool Repairing = false;
		TruckControls Controls;
		// World time of their last input. One not heard from in a while has left the wheel.
		double LastHeard = 0;
	};
	std::vector<CrewInput> Inputs;

	// Crewmates in the cab, as each machine shows them: a stand-in placed in this machine's own copy of
	// the cab every frame, from the cab-space spot the crewmate last sent. Placing them in cab space
	// keeps them aboard a truck moving far faster than a world position could keep up with.
	struct CabCrewmate
	{
		FP::PeerId Peer = 0;
		FP::int32 Crew = 0;
		bool InCab = false;
		FP::Vec3 CabPosition;
		float Yaw = 0;
		double LastHeard = 0;
		FP::Entity StandIn = FP::NullEntity;
	};
	std::vector<CabCrewmate> CabCrew;

	// On the host: crews whose truck is out of the race, delivered or not.
	bool Finished[CrewCount] = {};

	template <typename T>
	static T& ByPeer(std::vector<T>& list, FP::PeerId peer)
	{
		for (T& entry : list)
		{
			if (entry.Peer == peer)
				return entry;
		}
		list.push_back({});
		list.back().Peer = peer;
		return list.back();
	}
};
