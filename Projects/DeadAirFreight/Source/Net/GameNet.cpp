#include "Net/GameNet.h"
#include "Engine/Net/NetworkSystem.h"

using namespace FP;

namespace
{
	// The host as its claims say. One match at a time per process, so a plain value is enough.
	PeerId g_HostId = 0;

	NetworkSystem* FindNetwork(World& world)
	{
		for (const RefPtr<System>& system : world.GetSystems())
		{
			if (NetworkSystem* network = dynamic_cast<NetworkSystem*>(system.Get()))
				return network->GetSession().IsRunning() ? network : nullptr;
		}
		return nullptr;
	}
}

namespace GameNet
{
	bool IsNetworked(World& world)
	{
		return FindNetwork(world) != nullptr;
	}

	bool IsHost(World& world)
	{
		NetworkSystem* network = FindNetwork(world);
		return !network || network->GetSession().IsHosting();
	}

	PeerId GetHostId(World& world)
	{
		NetworkSystem* network = FindNetwork(world);
		if (!network)
			return 0;
		return network->GetSession().IsHosting() ? network->GetSession().GetLocalId() : g_HostId;
	}

	void SetHostId(PeerId host)
	{
		g_HostId = host;
	}

	PeerId GetLocalId(World& world)
	{
		NetworkSystem* network = FindNetwork(world);
		return network ? network->GetSession().GetLocalId() : 0;
	}

	NetWriter Begin(Message message)
	{
		NetWriter writer;
		writer.WriteU8(static_cast<uint8>(message));
		return writer;
	}

	void Send(World& world, const NetWriter& message, NetChannel channel, PeerId to)
	{
		if (NetworkSystem* network = FindNetwork(world))
			network->SendGame(channel, message.Data(), message.Size(), to);
	}

	void SendToHost(World& world, const NetWriter& message, NetChannel channel)
	{
		NetworkSystem* network = FindNetwork(world);
		if (network && !IsHost(world) && g_HostId != 0)
			network->SendGame(channel, message.Data(), message.Size(), g_HostId);
	}

	bool Poll(World& world, NetEvent& out)
	{
		NetworkSystem* network = FindNetwork(world);
		return network && network->PollGame(out);
	}
}
