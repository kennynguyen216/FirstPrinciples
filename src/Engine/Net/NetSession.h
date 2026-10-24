#pragma once

#include "Engine/Net/NetConnection.h"
#include "Engine/Net/NetCrypto.h"
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
		// A peer never finished connecting: nobody answered, the session was full, or its password
		// did not match ours.
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
	// Everything after the handshake is encrypted. Hello and Welcome swap fresh X25519 public keys,
	// each side derives a key per direction, and from then on every packet is sealed with
	// XChaCha20-Poly1305. A peer only counts as joined once a packet from it opens, which proves it
	// holds the matching keys. Changed, forged or replayed packets are dropped.
	//
	// Without a password this stops anyone listening in or tampering, but not someone who sits in
	// the middle from the very first packet. SetPassword closes that gap: the password is mixed into
	// the keys, so only peers that know it can talk.
	//
	// Every packet starts with a magic number, a type and the sender's peer id, in the clear but
	// covered by the seal. Data packets carry a NetConnection packet, and each message in it starts
	// with one byte saying whether it is the game's or the session's own (the peer lists used for
	// introductions).
	class NetSession
	{
	public:
		static constexpr int32 MaxPeers = 16;
		// A peer silent for this long has left.
		static constexpr double Timeout = 5.0;

		~NetSession() { Stop(); }

		// Peers must share it to connect. Set it before Start. Empty means none.
		void SetPassword(std::string_view password);

		// Opens the socket and makes fresh keys. Port 0 lets the OS choose, see GetPort.
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
			// Set by the first packet that opens with our keys. Until then the peer is not reported.
			bool Verified = false;
			bool WarnedDecrypt = false;
			uint8 PublicKey[NetCrypto::KeySize] = {};
			NetCrypto::LinkKeys Keys;
			uint64 SendCounter = 0;
			NetCrypto::ReplayWindow Replay;
			NetConnection Connection;

			~Peer() { Keys.Wipe(); }
		};

		struct PendingJoin
		{
			NetAddress Address;
			double Started = -1;
			double LastHello = -1;
		};

		void SendHandshake(const NetAddress& to, PacketType type);
		void SendSealed(Peer& peer, PacketType type, const uint8* body, int32 size);
		void HandlePacket(const NetAddress& from, const uint8* data, int32 size, double now);
		Peer* FindPeer(const NetAddress& address);
		Peer* FindPeer(PeerId id);
		Peer* AddPeer(PeerId id, const NetAddress& address, const uint8* publicKey, double now);
		void OnVerified(Peer& peer);
		void RemovePeer(int32 index, bool notify);
		void SendPeerList(Peer& to, const Array<const Peer*>& peers);
		void HandleSessionMessage(Peer& from, NetReader& reader, double now);
		void QueueJoin(const NetAddress& address, double now);

		UdpSocket m_Socket;
		PeerId m_LocalId = 0;
		NetCrypto::KeyPair m_Keys;
		uint8 m_Password[NetCrypto::KeySize] = {};
		bool m_HasPassword = false;
		Array<Peer*> m_Peers;
		Array<PendingJoin> m_Pending;
		Array<NetEvent> m_Events;
		int32 m_EventHead = 0;
		double m_Now = 0;
	};
}
