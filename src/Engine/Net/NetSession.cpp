#include "Engine/Net/NetSession.h"
#include "HAL/PlatformMisc.h"

namespace FP
{
	namespace
	{
		// "FPN1". Packets without it are someone else's and get ignored.
		constexpr uint32 Magic = 0x314E5046;
		constexpr double HelloInterval = 0.25;
		constexpr double JoinTimeout = 10.0;

		// First byte of every message inside a data packet.
		constexpr uint8 GameMessage = 0;
		constexpr uint8 PeerListMessage = 1;

		void WriteHeader(NetWriter& out, uint8 type, PeerId sender)
		{
			out.WriteU32(Magic);
			out.WriteU8(type);
			out.WriteU64(sender);
		}

		std::string IdText(PeerId id)
		{
			char text[20];
			std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(id));
			return text;
		}
	}

	bool NetSession::Start(uint16 port)
	{
		Stop();
		if (!m_Socket.Open(port))
		{
			FP_LOG_ERROR("Could not open UDP port %u", port);
			return false;
		}
		do
			PlatformMisc::GetRandomBytes(&m_LocalId, sizeof(m_LocalId));
		while (m_LocalId == 0);
		FP_LOG("Network session on port %u as peer %s", m_Socket.GetPort(), IdText(m_LocalId).c_str());
		return true;
	}

	void NetSession::Stop()
	{
		if (!IsRunning())
			return;
		// Three tries since a goodbye is never resent. Peers that miss them all time out instead.
		for (Peer* peer : m_Peers)
		{
			for (int i = 0; i < 3; ++i)
				SendControl(peer->Address, PacketType::Bye);
		}
		while (!m_Peers.IsEmpty())
			RemovePeer(m_Peers.Num() - 1, false);
		m_Pending.Clear();
		m_Socket.Close();
	}

	void NetSession::Join(const NetAddress& address)
	{
		QueueJoin(address, -1);
	}

	void NetSession::QueueJoin(const NetAddress& address, double now)
	{
		if (FindPeer(address))
			return;
		for (const PendingJoin& pending : m_Pending)
		{
			if (pending.Address == address)
				return;
		}
		PendingJoin& pending = m_Pending.Emplace();
		pending.Address = address;
		pending.Started = now;
	}

	void NetSession::SendControl(const NetAddress& to, PacketType type)
	{
		NetWriter packet;
		WriteHeader(packet, static_cast<uint8>(type), m_LocalId);
		m_Socket.Send(to, packet.Data(), static_cast<usize>(packet.Size()));
	}

	NetSession::Peer* NetSession::FindPeer(const NetAddress& address)
	{
		for (Peer* peer : m_Peers)
		{
			if (peer->Address == address)
				return peer;
		}
		return nullptr;
	}

	NetSession::Peer* NetSession::FindPeer(PeerId id)
	{
		for (Peer* peer : m_Peers)
		{
			if (peer->Id == id)
				return peer;
		}
		return nullptr;
	}

	const NetConnection* NetSession::GetConnection(PeerId id) const
	{
		for (const Peer* peer : m_Peers)
		{
			if (peer->Id == id)
				return &peer->Connection;
		}
		return nullptr;
	}

	Array<PeerId> NetSession::GetPeers() const
	{
		Array<PeerId> ids;
		for (const Peer* peer : m_Peers)
			ids.Add(peer->Id);
		return ids;
	}

	PeerId NetSession::GetAuthority() const
	{
		PeerId lowest = m_LocalId;
		for (const Peer* peer : m_Peers)
			lowest = peer->Id < lowest ? peer->Id : lowest;
		return lowest;
	}

	NetSession::Peer* NetSession::AddPeer(PeerId id, const NetAddress& address, double now)
	{
		Peer* peer = new Peer();
		peer->Id = id;
		peer->Address = address;
		peer->Joined = now;
		m_Peers.Add(peer);
		m_Pending.RemoveAll([&](const PendingJoin& pending) { return pending.Address == address; });
		FP_LOG("Peer %s joined from %s", IdText(id).c_str(), address.ToString().c_str());

		NetEvent& event = m_Events.Emplace();
		event.Type = NetEventType::PeerJoined;
		event.Peer = id;
		event.Address = address;

		// Introduce the newcomer to everyone we know, and everyone we know to the newcomer.
		Array<const Peer*> others;
		for (Peer* other : m_Peers)
		{
			if (other != peer)
			{
				others.Add(other);
				SendPeerList(*other, { peer });
			}
		}
		if (!others.IsEmpty())
			SendPeerList(*peer, others);
		return peer;
	}

	void NetSession::RemovePeer(int32 index, bool notify)
	{
		Peer* peer = m_Peers[index];
		if (notify)
		{
			FP_LOG("Peer %s left", IdText(peer->Id).c_str());
			NetEvent& event = m_Events.Emplace();
			event.Type = NetEventType::PeerLeft;
			event.Peer = peer->Id;
			event.Address = peer->Address;
		}
		delete peer;
		m_Peers.RemoveAt(index);
	}

	void NetSession::SendPeerList(Peer& to, const Array<const Peer*>& peers)
	{
		NetWriter message;
		message.WriteU8(PeerListMessage);
		message.WriteU8(static_cast<uint8>(peers.Num()));
		for (const Peer* peer : peers)
		{
			message.WriteU64(peer->Id);
			message.WriteU32(peer->Address.Ip);
			message.WriteU16(peer->Address.Port);
		}
		to.Connection.Send(NetChannel::Reliable, message.Data(), message.Size());
	}

	void NetSession::HandleSessionMessage(Peer&, NetReader& reader, double now)
	{
		const uint8 count = reader.ReadU8();
		for (uint8 i = 0; i < count && reader.IsValid(); ++i)
		{
			const PeerId id = reader.ReadU64();
			NetAddress address;
			address.Ip = reader.ReadU32();
			address.Port = reader.ReadU16();
			if (reader.IsValid() && id != m_LocalId && !FindPeer(id) && address.IsValid())
				QueueJoin(address, now);
		}
	}

	void NetSession::HandlePacket(const NetAddress& from, const uint8* data, int32 size, double now)
	{
		NetReader reader(data, size);
		const uint32 magic = reader.ReadU32();
		const PacketType type = static_cast<PacketType>(reader.ReadU8());
		const PeerId sender = reader.ReadU64();
		if (!reader.IsValid() || magic != Magic || sender == 0 || sender == m_LocalId)
			return;

		auto indexOf = [this](const Peer* peer) { return m_Peers.Find(const_cast<Peer*>(peer)); };
		Peer* peer = FindPeer(from);

		switch (type)
		{
			case PacketType::Hello:
				// Same address with a new id means that peer restarted.
				if (peer && peer->Id != sender)
				{
					RemovePeer(indexOf(peer), true);
					peer = nullptr;
				}
				if (!peer)
				{
					if (m_Peers.Num() >= MaxPeers)
					{
						SendControl(from, PacketType::Full);
						return;
					}
					AddPeer(sender, from, now);
				}
				// Sent for every hello, since the hello may be a retry after our last welcome got lost.
				SendControl(from, PacketType::Welcome);
				break;

			case PacketType::Welcome:
				if (!peer && m_Pending.FindIf([&](const PendingJoin& pending) { return pending.Address == from; }) != INDEX_NONE)
					AddPeer(sender, from, now);
				break;

			case PacketType::Data:
			{
				if (!peer || peer->Id != sender || !peer->Connection.ReadPacket(reader, now))
					return;
				NetMessage message;
				while (peer->Connection.Receive(message))
				{
					NetReader body(message.Data.Data(), message.Data.Num());
					const uint8 kind = body.ReadU8();
					if (kind == PeerListMessage)
					{
						HandleSessionMessage(*peer, body, now);
					}
					else if (kind == GameMessage && body.IsValid())
					{
						NetEvent& event = m_Events.Emplace();
						event.Type = NetEventType::Message;
						event.Peer = peer->Id;
						event.Address = peer->Address;
						event.Channel = message.Channel;
						event.Data.Resize(body.Remaining());
						if (body.Remaining() > 0)
							std::memcpy(event.Data.Data(), body.ReadBytes(body.Remaining()), static_cast<usize>(event.Data.Num()));
					}
				}
				break;
			}

			case PacketType::Bye:
				if (peer && peer->Id == sender)
					RemovePeer(indexOf(peer), true);
				break;

			case PacketType::Full:
			{
				const int32 index = m_Pending.FindIf([&](const PendingJoin& pending) { return pending.Address == from; });
				if (index != INDEX_NONE)
				{
					FP_LOG_WARN("%s is full", from.ToString().c_str());
					NetEvent& event = m_Events.Emplace();
					event.Type = NetEventType::JoinFailed;
					event.Address = from;
					m_Pending.RemoveAt(index);
				}
				break;
			}
		}
	}

	void NetSession::Update()
	{
		Update(PlatformMisc::GetTimeSeconds());
	}

	void NetSession::Update(double now)
	{
		m_Now = now;
		if (!IsRunning())
			return;

		uint8 buffer[UdpSocket::MaxDatagram];
		NetAddress from;
		int32 size = 0;
		while ((size = m_Socket.Receive(from, buffer, sizeof(buffer))) >= 0)
			HandlePacket(from, buffer, size, now);

		// Keep saying hello to peers we are joining until they answer or we give up.
		for (int32 i = m_Pending.Num() - 1; i >= 0; --i)
		{
			PendingJoin& pending = m_Pending[i];
			if (pending.Started < 0)
				pending.Started = now;
			if (now - pending.Started > JoinTimeout)
			{
				FP_LOG_WARN("Nobody answered at %s", pending.Address.ToString().c_str());
				NetEvent& event = m_Events.Emplace();
				event.Type = NetEventType::JoinFailed;
				event.Address = pending.Address;
				m_Pending.RemoveAt(i);
			}
			else if (pending.LastHello < 0 || now - pending.LastHello >= HelloInterval)
			{
				SendControl(pending.Address, PacketType::Hello);
				pending.LastHello = now;
			}
		}

		for (int32 i = m_Peers.Num() - 1; i >= 0; --i)
		{
			const Peer* peer = m_Peers[i];
			const double lastHeard = peer->Connection.PacketsReceived > 0 ? peer->Connection.GetLastReceiveTime() : peer->Joined;
			if (now - lastHeard > Timeout)
				RemovePeer(i, true);
		}

		NetWriter packet;
		NetWriter framed;
		for (Peer* peer : m_Peers)
		{
			while (peer->Connection.WritePacket(packet, now))
			{
				framed.Clear();
				WriteHeader(framed, static_cast<uint8>(PacketType::Data), m_LocalId);
				framed.WriteBytes(packet.Data(), static_cast<usize>(packet.Size()));
				m_Socket.Send(peer->Address, framed.Data(), static_cast<usize>(framed.Size()));
			}
		}
	}

	bool NetSession::Send(PeerId id, NetChannel channel, const void* data, int32 size)
	{
		Peer* peer = FindPeer(id);
		if (!peer)
			return false;
		NetWriter message;
		message.WriteU8(GameMessage);
		message.WriteBytes(data, static_cast<usize>(size));
		return peer->Connection.Send(channel, message.Data(), message.Size());
	}

	void NetSession::Broadcast(NetChannel channel, const void* data, int32 size)
	{
		for (Peer* peer : m_Peers)
			Send(peer->Id, channel, data, size);
	}

	bool NetSession::PollEvent(NetEvent& out)
	{
		if (m_EventHead >= m_Events.Num())
			return false;
		out = std::move(m_Events[m_EventHead++]);
		if (m_EventHead == m_Events.Num())
		{
			m_Events.Clear();
			m_EventHead = 0;
		}
		return true;
	}
}
