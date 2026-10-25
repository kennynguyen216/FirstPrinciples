#include "Title/LanDiscovery.h"

#ifdef _WIN32
	#include <winsock2.h>
	#include <ws2tcpip.h>
#else
	#include <arpa/inet.h>
	#include <netinet/in.h>
	#include <sys/socket.h>
	#include <unistd.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace FP;

namespace
{
	// Every announcement starts with this, so stray traffic on the port is ignored.
	constexpr const char* Magic = "MKU1";
	constexpr double AnnounceInterval = 1.0;
	// A host silent this long has stopped hosting.
	constexpr double ForgetAfter = 3.5;
}

bool LanDiscovery::Listen(uint16 discoveryPort)
{
	if (m_Listener.IsOpen())
		return true;
	return m_Listener.Open(discoveryPort, true);
}

void LanDiscovery::Announce(uint16 discoveryPort, uint16 gamePort, int32 players, const std::string& name, double now)
{
	if (now < m_NextAnnounce)
		return;
	m_NextAnnounce = now + AnnounceInterval;
	if (!m_Announcer.IsOpen() && (!m_Announcer.Open(0) || !m_Announcer.EnableBroadcast()))
		return;
	char message[160];
	const int length = std::snprintf(message, sizeof(message), "%s %u %d %s", Magic, gamePort, players, name.c_str());
	if (length > 0)
		m_Announcer.Send(NetAddress::Broadcast(discoveryPort), message, static_cast<usize>(std::min<int>(length, sizeof(message) - 1)));
}

void LanDiscovery::Update(double now)
{
	char buffer[UdpSocket::MaxDatagram];
	NetAddress from;
	while (m_Listener.IsOpen())
	{
		const int32 read = m_Listener.Receive(from, buffer, sizeof(buffer) - 1);
		if (read < 0)
			break;
		buffer[read] = 0;
		char magic[8] = {};
		unsigned gamePort = 0;
		int players = 0;
		int nameStart = 0;
		if (std::sscanf(buffer, "%7s %u %d %n", magic, &gamePort, &players, &nameStart) < 3 || std::strcmp(magic, Magic) != 0 || gamePort == 0 || gamePort > 65535)
			continue;

		const NetAddress address{ from.Ip, static_cast<uint16>(gamePort) };
		auto found = std::find_if(m_Games.begin(), m_Games.end(), [&](const FoundGame& game) { return game.Address == address; });
		if (found == m_Games.end())
		{
			m_Games.push_back({});
			found = m_Games.end() - 1;
		}
		found->Address = address;
		found->Name = nameStart > 0 ? std::string(buffer + nameStart) : "Game";
		found->Players = players;
		found->LastHeard = now;
	}
	m_Games.erase(std::remove_if(m_Games.begin(), m_Games.end(), [&](const FoundGame& game) { return now - game.LastHeard > ForgetAfter; }), m_Games.end());
}

void LanDiscovery::Stop()
{
	m_Listener.Close();
	m_Announcer.Close();
	m_Games.clear();
}

std::string LanDiscovery::GetMachineName()
{
	for (const char* variable : { "COMPUTERNAME", "HOSTNAME", "USER" })
	{
		if (const char* value = std::getenv(variable); value && *value)
			return value;
	}
	return "Host";
}

std::string LanDiscovery::GetLocalAddress()
{
	// Aiming a UDP socket at an outside address sends nothing, but makes the system pick the network
	// it would use, and so the address this machine has there.
	std::string address;
#ifdef _WIN32
	const SOCKET probe = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (probe == INVALID_SOCKET)
		return address;
#else
	const int probe = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (probe < 0)
		return address;
#endif
	sockaddr_in outside = {};
	outside.sin_family = AF_INET;
	outside.sin_port = htons(53);
	inet_pton(AF_INET, "8.8.8.8", &outside.sin_addr);
	sockaddr_in local = {};
	socklen_t size = sizeof(local);
	if (connect(probe, reinterpret_cast<sockaddr*>(&outside), sizeof(outside)) == 0 &&
		getsockname(probe, reinterpret_cast<sockaddr*>(&local), &size) == 0 && local.sin_addr.s_addr != 0)
	{
		char text[INET_ADDRSTRLEN] = {};
		if (inet_ntop(AF_INET, &local.sin_addr, text, sizeof(text)))
			address = text;
	}
#ifdef _WIN32
	closesocket(probe);
#else
	close(probe);
#endif
	return address;
}
