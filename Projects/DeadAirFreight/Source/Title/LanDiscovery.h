#pragma once

#include "HAL/PlatformSocket.h"
#include "Core/CoreMinimal.h"

#include <string>
#include <vector>

// Finds games on the local network. A host announces itself to the whole LAN about once a second;
// the title screen listens and lists every host it heard from lately.
class LanDiscovery
{
public:
	struct FoundGame
	{
		std::string Name;
		FP::NetAddress Address; // where to join: the host's address and game port
		FP::int32 Players = 0;
		double LastHeard = 0;
	};

	// Start hearing announcements on the discovery port. Shared, so several games on one machine
	// can all listen.
	bool Listen(FP::uint16 discoveryPort);
	// Announce a hosted game to the LAN. Call as often as wanted; it sends about once a second.
	void Announce(FP::uint16 discoveryPort, FP::uint16 gamePort, FP::int32 players, const std::string& name, double now);
	// Reads waiting announcements and forgets hosts not heard from for a few seconds.
	void Update(double now);
	void Stop();

	const std::vector<FoundGame>& GetGames() const { return m_Games; }

	// This machine's name, for announcing.
	static std::string GetMachineName();
	// This machine's address on its network, the one others type to join, or empty when offline.
	static std::string GetLocalAddress();

private:
	FP::UdpSocket m_Listener;
	FP::UdpSocket m_Announcer;
	double m_NextAnnounce = 0;
	std::vector<FoundGame> m_Games;
};
