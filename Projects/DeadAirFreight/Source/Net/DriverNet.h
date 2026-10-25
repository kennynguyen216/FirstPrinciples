#pragma once

#include "Driver/DriverSystem.h"
#include "Engine/Net/NetSession.h"

#include <string>
#include <vector>

// What the network and the driver bridge share about the truck, on the Match entity while playing.
struct DriverNetComponent
{
	// This machine's Driver systems, once launched.
	DriverSystem* Driver = nullptr;

	// Away from the host: the host's match package, which arrives in pieces before the cab can start.
	std::string PackageText;
	FP::int32 PackagePieces = 0;
	FP::int32 PackageTotal = -1;
	bool PackageReady = false;
	MatchPackage Package;

	// Away from the host: the newest truck from the host.
	bool HasTruck = false;
	TruckState Truck;
	LifeSupport Life;
	// The peer at the wheel, or 0 when nobody is.
	FP::PeerId WheelHolder = 0;
	// Why the host's match ended, waiting to be shown.
	std::string Results;

	// On the host: crewmates on other machines and what they did last.
	struct RemoteCrew
	{
		FP::PeerId Peer = 0;
		bool Seated = false;
		bool Repairing = false;
		TruckControls Controls;
		// World time of their last input. One not heard from in a while has left the wheel.
		double LastHeard = 0;
	};
	std::vector<RemoteCrew> Remote;

	// Crewmates in the cab, as each machine shows them: a stand-in placed in this machine's own copy of
	// the cab every frame, from the cab-space spot the crewmate last sent. Placing them in cab space
	// keeps them aboard a truck moving far faster than a world position could keep up with.
	struct CabCrewmate
	{
		FP::PeerId Peer = 0;
		bool InCab = false;
		FP::Vec3 CabPosition;
		float Yaw = 0;
		double LastHeard = 0;
		FP::Entity StandIn = FP::NullEntity;
	};
	std::vector<CabCrewmate> CabCrew;

	CabCrewmate& GetCabCrewmate(FP::PeerId peer)
	{
		for (CabCrewmate& crewmate : CabCrew)
		{
			if (crewmate.Peer == peer)
				return crewmate;
		}
		CabCrew.push_back({ peer });
		return CabCrew.back();
	}

	RemoteCrew& GetRemote(FP::PeerId peer)
	{
		for (RemoteCrew& crew : Remote)
		{
			if (crew.Peer == peer)
				return crew;
		}
		Remote.push_back({ peer });
		return Remote.back();
	}
};
