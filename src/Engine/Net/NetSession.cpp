#include "Engine/Net/NetSession.h"
#include "HAL/PlatformMisc.h"

#include <cstdio>
#include <cstring>

namespace FP
{
	namespace
	{
		// "FPN2". Packets without it are someone else's, or from the unencrypted first version.
		constexpr uint32 Magic = 0x324E5046;
		constexpr int32 HeaderSize = 13;
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

	void NetSession::SetPassword(std::string_view password)
	{
		m_HasPassword = !password.empty();
		if (m_HasPassword)
			NetCrypto::HashPassword(password, m_Password);
		else
			std::memset(m_Password, 0, sizeof(m_Password));
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
		// New keys every session, so a key leaked later never opens packets recorded earlier.
		m_Keys = NetCrypto::KeyPair::Generate();
		FP_LOG("Network session on port %u as peer %s%s", m_Socket.GetPort(), IdText(m_LocalId).c_str(), m_HasPassword ? " with a password" : "");
		return true;
	}

	void NetSession::Stop()
	{
		if (!IsRunning())
			return;
		// Three tries since a goodbye is never resent. Peers that miss them all time out instead.
		for (Peer* peer : m_Peers)
		{
			for (int i = 0; peer->Verified && i < 3; ++i)
				SendSealed(*peer, PacketType::Bye, nullptr, 0);
		}
		while (!m_Peers.IsEmpty())
			RemovePeer(m_Peers.Num() - 1, false);
		m_Pending.Clear();
		m_Keys.Wipe();
		m_Socket.Close();
		m_Joined = false;
	}

	void NetSession::Join(const NetAddress& address)
	{
		m_Joined = true;
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

	void NetSession::SendHandshake(const NetAddress& to, PacketType type)
	{
		NetWriter packet;
		WriteHeader(packet, static_cast<uint8>(type), m_LocalId);
		if (type != PacketType::Full)
			packet.WriteBytes(m_Keys.Public, NetCrypto::KeySize);
		m_Socket.Send(to, packet.Data(), static_cast<usize>(packet.Size()));
	}

	void NetSession::SendSealed(Peer& peer, PacketType type, const uint8* body, int32 size)
	{
		NetWriter header;
		WriteHeader(header, static_cast<uint8>(type), m_LocalId);
		Array<uint8> packet;
		packet.Resize(header.Size());
		std::memcpy(packet.Data(), header.Data(), static_cast<usize>(header.Size()));
		NetCrypto::Seal(peer.Keys.Send, peer.SendCounter++, header.Data(), header.Size(), body, size, packet);
		m_Socket.Send(peer.Address, packet.Data(), static_cast<usize>(packet.Num()));
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
			if (peer->Id == id && peer->Verified)
				return &peer->Connection;
		}
		return nullptr;
	}

	Array<PeerId> NetSession::GetPeers() const
	{
		Array<PeerId> ids;
		for (const Peer* peer : m_Peers)
		{
			if (peer->Verified)
				ids.Add(peer->Id);
		}
		return ids;
	}

	PeerId NetSession::GetAuthority() const
	{
		PeerId lowest = m_LocalId;
		for (const Peer* peer : m_Peers)
			lowest = peer->Verified && peer->Id < lowest ? peer->Id : lowest;
		return lowest;
	}

	NetSession::Peer* NetSession::AddPeer(PeerId id, const NetAddress& address, const uint8* publicKey, double now)
	{
		Peer* peer = new Peer();
		if (!NetCrypto::DeriveLinkKeys(m_Keys, publicKey, m_Password, m_HasPassword, peer->Keys))
		{
			delete peer;
			return nullptr;
		}
		peer->Id = id;
		peer->Address = address;
		peer->Joined = now;
		std::memcpy(peer->PublicKey, publicKey, NetCrypto::KeySize);
		m_Peers.Add(peer);
		m_Pending.RemoveAll([&](const PendingJoin& pending) { return pending.Address == address; });
		return peer;
	}

	void NetSession::OnVerified(Peer& peer)
	{
		peer.Verified = true;
		FP_LOG("Peer %s joined from %s", IdText(peer.Id).c_str(), peer.Address.ToString().c_str());
		NetEvent& event = m_Events.Emplace();
		event.Type = NetEventType::PeerJoined;
		event.Peer = peer.Id;
		event.Address = peer.Address;

		// Introduce the newcomer to everyone we know, and everyone we know to the newcomer.
		// Only verified peers, so nobody learns addresses without the keys.
		Array<const Peer*> others;
		for (Peer* other : m_Peers)
		{
			if (other != &peer && other->Verified)
			{
				others.Add(other);
				SendPeerList(*other, { &peer });
			}
		}
		if (!others.IsEmpty())
			SendPeerList(peer, others);
	}

	void NetSession::RemovePeer(int32 index, bool notify)
	{
		Peer* peer = m_Peers[index];
		if (notify && peer->Verified)
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

		Peer* peer = FindPeer(from);
		auto remove = [this](Peer* gone) { RemovePeer(m_Peers.Find(gone), true); };

		switch (type)
		{
			case PacketType::Hello:
			case PacketType::Welcome:
			{
				const uint8* publicKey = reader.ReadBytes(NetCrypto::KeySize);
				if (!publicKey)
					return;
				// Same address with a new id or new keys. A handshake is not signed, so this could be
				// anyone, and a verified peer is never dropped for it. A peer that really restarted
				// gets back in once its old entry times out.
				if (peer && (peer->Id != sender || std::memcmp(peer->PublicKey, publicKey, NetCrypto::KeySize) != 0))
				{
					if (peer->Verified)
						return;
					remove(peer);
					peer = nullptr;
				}
				const bool expected = m_Pending.FindIf([&](const PendingJoin& pending) { return pending.Address == from; }) != INDEX_NONE;
				if (!peer && (type == PacketType::Hello || expected))
				{
					if (m_Peers.Num() >= MaxPeers)
					{
						SendHandshake(from, PacketType::Full);
						return;
					}
					peer = AddPeer(sender, from, publicKey, now);
				}
				// Every hello gets a welcome, since the hello may be a retry after our last welcome got lost.
				if (peer && type == PacketType::Hello)
					SendHandshake(from, PacketType::Welcome);
				break;
			}

			case PacketType::Data:
			case PacketType::Bye:
			{
				if (!peer || peer->Id != sender)
					return;
				uint64 counter = 0;
				Array<uint8> body;
				if (!NetCrypto::Open(peer->Keys.Receive, data, HeaderSize, data + HeaderSize, size - HeaderSize, counter, body))
				{
					if (!peer->Verified && !peer->WarnedDecrypt)
					{
						FP_LOG_WARN("Packets from %s do not decrypt. Is the password the same on both sides?", from.ToString().c_str());
						peer->WarnedDecrypt = true;
					}
					return;
				}
				if (!peer->Replay.Accept(counter))
					return;
				if (!peer->Verified)
					OnVerified(*peer);
				if (type == PacketType::Bye)
				{
					remove(peer);
					return;
				}

				NetReader inner(body.Data(), body.Num());
				if (!peer->Connection.ReadPacket(inner, now))
					return;
				NetMessage message;
				while (peer->Connection.Receive(message))
				{
					NetReader content(message.Data.Data(), message.Data.Num());
					const uint8 kind = content.ReadU8();
					if (kind == PeerListMessage)
					{
						HandleSessionMessage(*peer, content, now);
					}
					else if (kind == GameMessage && content.IsValid())
					{
						NetEvent& event = m_Events.Emplace();
						event.Type = NetEventType::Message;
						event.Peer = peer->Id;
						event.Address = peer->Address;
						event.Channel = message.Channel;
						event.Data.Resize(content.Remaining());
						if (content.Remaining() > 0)
							std::memcpy(event.Data.Data(), content.ReadBytes(content.Remaining()), static_cast<usize>(event.Data.Num()));
					}
				}
				break;
			}

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
				SendHandshake(pending.Address, PacketType::Hello);
				pending.LastHello = now;
			}
		}

		for (int32 i = m_Peers.Num() - 1; i >= 0; --i)
		{
			const Peer* peer = m_Peers[i];
			const double lastHeard = peer->Connection.PacketsReceived > 0 ? peer->Connection.GetLastReceiveTime() : peer->Joined;
			if (now - lastHeard <= Timeout)
				continue;
			if (!peer->Verified)
			{
				// Keys never matched, or it went away mid handshake.
				FP_LOG_WARN("Could not connect to %s", peer->Address.ToString().c_str());
				NetEvent& event = m_Events.Emplace();
				event.Type = NetEventType::JoinFailed;
				event.Address = peer->Address;
			}
			RemovePeer(i, true);
		}

		// Unverified peers get packets too. The first one that opens on their side verifies us.
		NetWriter packet;
		for (Peer* peer : m_Peers)
		{
			while (peer->Connection.WritePacket(packet, now))
				SendSealed(*peer, PacketType::Data, packet.Data(), packet.Size());
		}
	}

	bool NetSession::Send(PeerId id, NetChannel channel, const void* data, int32 size)
	{
		Peer* peer = FindPeer(id);
		if (!peer || !peer->Verified)
			return false;
		NetWriter message;
		message.WriteU8(GameMessage);
		message.WriteBytes(data, static_cast<usize>(size));
		return peer->Connection.Send(channel, message.Data(), message.Size());
	}

	void NetSession::Broadcast(NetChannel channel, const void* data, int32 size)
	{
		for (Peer* peer : m_Peers)
		{
			if (peer->Verified)
				Send(peer->Id, channel, data, size);
		}
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
