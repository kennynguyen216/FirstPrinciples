#include "Title/LanDiscovery.h"

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
