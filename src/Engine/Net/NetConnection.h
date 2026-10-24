#pragma once

#include "Engine/Net/NetBuffer.h"

namespace FP
{
	enum class NetChannel : uint8
	{
		Unreliable, // may be lost or arrive out of order, never duplicated
		Sequenced,  // may be lost, older than the newest one received is dropped. Good for state.
		Reliable,   // always arrives, exactly once, in the order sent
	};

	struct NetMessage
	{
		NetChannel Channel = NetChannel::Unreliable;
		Array<uint8> Data;
	};

	// The reliability layer for a link to one peer. It turns messages into packets and back and
	// never touches a socket, so the session can send its packets over UDP and tests can push them
	// through a link that drops, duplicates and reorders.
	//
	// Every packet carries its own sequence number plus an ack for the newest packet received and
	// a bitfield for the 32 before it, so one lost ack is covered by the next packet. Reliable
	// messages are resent until a packet holding them is acked, then delivered in order.
	//
	// Packet layout: u16 sequence, u16 ack, u32 ack bits, u8 flags, then messages until the end.
	// Each message is u8 channel, u16 id (not for Unreliable), u16 length and the bytes.
	class NetConnection
	{
	public:
		// Stays under the usual 1500 byte MTU with room for IP, UDP and session headers.
		static constexpr int32 MaxPacketSize = 1200;
		// No fragmentation, so one message must fit in one packet.
		static constexpr int32 MaxMessageSize = 1024;
		// Reliable messages in flight. Further ones wait until older ones are acked.
		static constexpr int32 ReliableWindow = 256;

		NetConnection();

		// Queues a message. False when it is too big.
		bool Send(NetChannel channel, const void* data, int32 size);

		// Builds the next packet into out. False when there is nothing worth sending yet.
		// Call it until it returns false to flush everything queued.
		bool WritePacket(NetWriter& out, double now);

		// Takes a packet from the peer. False when it is malformed, and then nothing in it is used.
		bool ReadPacket(NetReader& in, double now);

		// Next message that arrived, in the order the channels allow.
		bool Receive(NetMessage& out);

		// Smoothed round trip time in seconds.
		float GetRtt() const { return m_Rtt; }
		// Share of recent packets that were never acked, from 0 to 1.
		float GetPacketLoss() const { return m_PacketLoss; }
		double GetLastReceiveTime() const { return m_LastReceiveTime; }
		int32 GetPendingReliable() const { return m_ReliableQueue.Num(); }

		uint64 PacketsSent = 0;
		uint64 PacketsReceived = 0;
		uint64 ReliableResends = 0;

	private:
		static constexpr int32 PacketHistory = 1024;
		static constexpr int32 MaxReliablePerPacket = 64;
		// A packet with nothing in it still goes out this often, so acks flow and the link stays alive.
		static constexpr double KeepAliveInterval = 0.1;

		struct SentPacket
		{
			int32 Sequence = -1;
			bool Acked = false;
			double Time = 0;
			int32 ReliableCount = 0;
			uint16 ReliableIds[MaxReliablePerPacket];
		};

		struct QueuedMessage
		{
			NetChannel Channel = NetChannel::Unreliable;
			uint16 Id = 0;
			Array<uint8> Data;
		};

		struct PendingReliable
		{
			uint16 Id = 0;
			bool Acked = false;
			double LastSent = -1;
			Array<uint8> Data;
		};

		struct ReceivedReliable
		{
			bool Present = false;
			Array<uint8> Data;
		};

		void OnPacketAcked(uint16 sequence, double now);
		void CountLostPackets();
		double ResendDelay() const;

		// Sending.
		uint16 m_LocalSequence = 0;
		Array<SentPacket> m_Sent;
		uint16 m_NextReliableId = 0;
		Array<PendingReliable> m_ReliableQueue; // oldest unacked first
		Array<QueuedMessage> m_Unreliable;
		uint16 m_NextSequencedId = 0;
		double m_LastSendTime = -1;
		bool m_AckOwed = false;

		// Receiving.
		bool m_HasReceived = false;
		uint16 m_RemoteSequence = 0;
		Array<int32> m_Received; // sequence held in each slot, or -1
		uint16 m_NextExpectedReliable = 0;
		Array<ReceivedReliable> m_ReliableBuffer;
		bool m_HasSequenced = false;
		uint16 m_LastSequenced = 0;
		Array<NetMessage> m_Delivered;
		int32 m_DeliveredHead = 0;
		double m_LastReceiveTime = 0;

		// Link quality.
		float m_Rtt = 0.1f;
		bool m_HasRtt = false;
		float m_PacketLoss = 0;
		bool m_HasAck = false;
		uint16 m_HighestAck = 0;
		uint16 m_NextLossCheck = 0;
	};
}
