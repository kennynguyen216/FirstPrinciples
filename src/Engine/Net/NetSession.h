#pragma once

#include "Engine/Net/NetConnection.h"
#include "HAL/PlatformSocket.h"

namespace FP
{
	// Random id each peer picks when its session starts.
	using PeerId = uint64;

	enum class NetEventType
	{
		PeerJoined,
		PeerLeft,
		Message,
		// Join gave up because nobody answered, or the session was full.
		JoinFailed,
	};

	struct NetEvent
	{
		NetEventType Type = NetEventType::Message;
		PeerId Peer = 0;
		NetAddress Address;
		NetChannel Channel = NetChannel::Unreliable;
		Array<uint8> Data;
	};

	// Peer to peer session over one UDP socket. Every peer talks to every other peer directly.
	//
	// Start one session, then Join any peer already in a game. That peer tells everyone else about
	// the newcomer and the newcomer about everyone else, and both sides of each pair then say hello
	// at the same time. That also opens the way through most home routers (UDP hole punching).
	// There is no relay, so peers behind strict NATs cannot reach each other.
	//
	// Every packet starts with a magic number, a type and the sender's peer id. Data packets carry a
	// NetConnection packet, and each message in it starts with one byte saying whether it is the
	// game's or the session's own (the peer lists used for introductions).
	class NetSession
	{
	public:
		static constexpr int32 MaxPeers = 16;
		// A peer silent for this long has left.
		static constexpr double Timeout = 5.0;

		~NetSession() { Stop(); }

		// Opens the socket. Port 0 lets the OS choose, see GetPort.
		bool Start(uint16 port);

		// Says goodbye to everyone and closes the socket.
		void Stop();

		// Connects to a peer in a game. Everyone already there connects to us as well.
		void Join(const NetAddress& address);

		// Receives packets, runs handshakes and timeouts and sends what is queued.
		// Call once a frame. The version without a time uses the system clock.
		void Update();
		void Update(double now);

		// One byte of each message is the session's, so game messages hold up to MaxMessageSize - 1.
		bool Send(PeerId peer, NetChannel channel, const void* data, int32 size);
		void Broadcast(NetChannel channel, const void* data, int32 size);

		bool PollEvent(NetEvent& out);

		bool IsRunning() const { return m_Socket.IsOpen(); }
		uint16 GetPort() const { return m_Socket.GetPort(); }
		PeerId GetLocalId() const { return m_LocalId; }
		Array<PeerId> GetPeers() const;
		const NetConnection* GetConnection(PeerId peer) const;

		// The lowest id among this peer and everyone connected. All peers agree on it without
		// talking, so it decides who runs shared things, and it moves on when that peer leaves.
		PeerId GetAuthority() const;

	private:
		enum class PacketType : uint8
		{
			Hello,
			Welcome,
			Data,
			Bye,
			Full,
		};

		struct Peer
		{
			PeerId Id = 0;
			NetAddress Address;
			double Joined = 0;
			NetConnection Connection;
		};

		struct PendingJoin
		{
			NetAddress Address;
			double Started = -1;
			double LastHello = -1;
		};

		void SendControl(const NetAddress& to, PacketType type);
		void HandlePacket(const NetAddress& from, const uint8* data, int32 size, double now);
		Peer* FindPeer(const NetAddress& address);
		Peer* FindPeer(PeerId id);
		Peer* AddPeer(PeerId id, const NetAddress& address, double now);
		void RemovePeer(int32 index, bool notify);
		void SendPeerList(Peer& to, const Array<const Peer*>& peers);
		void HandleSessionMessage(Peer& from, NetReader& reader, double now);
		void QueueJoin(const NetAddress& address, double now);

		UdpSocket m_Socket;
		PeerId m_LocalId = 0;
		Array<Peer*> m_Peers;
		Array<PendingJoin> m_Pending;
		Array<NetEvent> m_Events;
		int32 m_EventHead = 0;
		double m_Now = 0;
	};
}
