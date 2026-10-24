#include "Engine/Net/NetConnection.h"

#include <algorithm>

namespace FP
{
	namespace
	{
		constexpr uint8 FlagHasAck = 1;
		constexpr int32 HeaderSize = 9;

		int32 MessageOverhead(NetChannel channel) { return channel == NetChannel::Unreliable ? 3 : 5; }
	}

	NetConnection::NetConnection()
	{
		m_Sent.Resize(PacketHistory);
		m_Received.Resize(PacketHistory);
		for (int32& sequence : m_Received)
			sequence = -1;
		m_ReliableBuffer.Resize(ReliableWindow);
	}

	bool NetConnection::Send(NetChannel channel, const void* data, int32 size)
	{
		if (size < 0 || size > MaxMessageSize)
		{
			FP_LOG_ERROR("Network message of %d bytes is over the %d byte limit", size, MaxMessageSize);
			return false;
		}
		Array<uint8> bytes;
		bytes.Resize(size);
		if (size > 0)
			std::memcpy(bytes.Data(), data, static_cast<usize>(size));

		if (channel == NetChannel::Reliable)
		{
			PendingReliable& message = m_ReliableQueue.Emplace();
			message.Id = m_NextReliableId++;
			message.Data = std::move(bytes);
		}
		else
		{
			QueuedMessage& message = m_Unreliable.Emplace();
			message.Channel = channel;
			message.Id = channel == NetChannel::Sequenced ? m_NextSequencedId++ : 0;
			message.Data = std::move(bytes);
		}
		return true;
	}

	double NetConnection::ResendDelay() const
	{
		return std::clamp(static_cast<double>(m_Rtt) * 1.5 + 0.02, 0.05, 1.0);
	}

	bool NetConnection::WritePacket(NetWriter& out, double now)
	{
		out.Clear();
		out.WriteU16(m_LocalSequence);
		out.WriteU16(m_RemoteSequence);
		uint32 ackBits = 0;
		for (int32 i = 0; i < 32 && m_HasReceived; ++i)
		{
			const uint16 sequence = static_cast<uint16>(m_RemoteSequence - 1 - i);
			if (m_Received[sequence % PacketHistory] == sequence)
				ackBits |= 1u << i;
		}
		out.WriteU32(ackBits);
		out.WriteU8(m_HasReceived ? FlagHasAck : 0);

		SentPacket& record = m_Sent[m_LocalSequence % PacketHistory];
		record.Sequence = m_LocalSequence;
		record.Acked = false;
		record.Time = now;
		record.ReliableCount = 0;

		int32 room = MaxPacketSize - HeaderSize;
		bool hasData = false;

		// Reliable messages first: new ones and any whose packet looks lost.
		const double resend = ResendDelay();
		for (int32 i = 0; i < m_ReliableQueue.Num() && i < ReliableWindow && record.ReliableCount < MaxReliablePerPacket; ++i)
		{
			PendingReliable& message = m_ReliableQueue[i];
			if (message.Acked || (message.LastSent >= 0 && now - message.LastSent < resend))
				continue;
			const int32 size = MessageOverhead(NetChannel::Reliable) + message.Data.Num();
			if (size > room)
				break;
			out.WriteU8(static_cast<uint8>(NetChannel::Reliable));
			out.WriteU16(message.Id);
			out.WriteU16(static_cast<uint16>(message.Data.Num()));
			out.WriteBytes(message.Data.Data(), static_cast<usize>(message.Data.Num()));
			room -= size;
			if (message.LastSent >= 0)
				++ReliableResends;
			message.LastSent = now;
			record.ReliableIds[record.ReliableCount++] = message.Id;
			hasData = true;
		}

		// Then unreliable ones in the order they were sent. What does not fit waits for the next packet.
		int32 written = 0;
		for (; written < m_Unreliable.Num(); ++written)
		{
			const QueuedMessage& message = m_Unreliable[written];
			const int32 size = MessageOverhead(message.Channel) + message.Data.Num();
			if (size > room)
				break;
			out.WriteU8(static_cast<uint8>(message.Channel));
			if (message.Channel != NetChannel::Unreliable)
				out.WriteU16(message.Id);
			out.WriteU16(static_cast<uint16>(message.Data.Num()));
			out.WriteBytes(message.Data.Data(), static_cast<usize>(message.Data.Num()));
			room -= size;
			hasData = true;
		}
		if (written > 0)
		{
			Array<QueuedMessage> rest;
			for (int32 i = written; i < m_Unreliable.Num(); ++i)
				rest.Add(std::move(m_Unreliable[i]));
			m_Unreliable = std::move(rest);
		}

		const bool keepAliveDue = m_LastSendTime < 0 || now - m_LastSendTime >= KeepAliveInterval;
		if (!hasData && !m_AckOwed && !keepAliveDue)
		{
			record.Sequence = -1;
			out.Clear();
			return false;
		}
		++m_LocalSequence;
		++PacketsSent;
		m_LastSendTime = now;
		m_AckOwed = false;
		return true;
	}

	void NetConnection::OnPacketAcked(uint16 sequence, double now)
	{
		SentPacket& record = m_Sent[sequence % PacketHistory];
		if (record.Sequence != sequence || record.Acked)
			return;
		record.Acked = true;

		const float sample = static_cast<float>(now - record.Time);
		m_Rtt = m_HasRtt ? m_Rtt + (sample - m_Rtt) * 0.1f : sample;
		m_HasRtt = true;

		for (int32 i = 0; i < record.ReliableCount && !m_ReliableQueue.IsEmpty(); ++i)
		{
			const uint16 index = static_cast<uint16>(record.ReliableIds[i] - m_ReliableQueue[0].Id);
			if (index < m_ReliableQueue.Num() && m_ReliableQueue[index].Id == record.ReliableIds[i])
				m_ReliableQueue[index].Acked = true;
		}
	}

	void NetConnection::CountLostPackets()
	{
		// A packet more than 32 behind the newest ack can no longer be acked, so it was lost.
		const uint16 settled = static_cast<uint16>(m_HighestAck - 33);
		while (!SequenceGreater(m_NextLossCheck, settled) && SequenceLess(m_NextLossCheck, m_LocalSequence))
		{
			const SentPacket& record = m_Sent[m_NextLossCheck % PacketHistory];
			if (record.Sequence == m_NextLossCheck)
				m_PacketLoss += ((record.Acked ? 0.0f : 1.0f) - m_PacketLoss) * 0.05f;
			++m_NextLossCheck;
		}
	}

	bool NetConnection::ReadPacket(NetReader& in, double now)
	{
		const uint16 sequence = in.ReadU16();
		const uint16 ack = in.ReadU16();
		const uint32 ackBits = in.ReadU32();
		const uint8 flags = in.ReadU8();

		// Parse everything before using any of it, so a broken packet changes nothing.
		struct Parsed
		{
			NetChannel Channel;
			uint16 Id;
			const uint8* Bytes;
			int32 Size;
		};
		Array<Parsed> messages;
		while (in.IsValid() && in.Remaining() > 0)
		{
			const uint8 channel = in.ReadU8();
			if (channel > static_cast<uint8>(NetChannel::Reliable))
				return false;
			Parsed& message = messages.Emplace();
			message.Channel = static_cast<NetChannel>(channel);
			message.Id = message.Channel == NetChannel::Unreliable ? 0 : in.ReadU16();
			message.Size = in.ReadU16();
			if (message.Size > MaxMessageSize)
				return false;
			message.Bytes = in.ReadBytes(message.Size);
		}
		if (!in.IsValid())
			return false;

		m_LastReceiveTime = now;
		++PacketsReceived;

		if (flags & FlagHasAck)
		{
			OnPacketAcked(ack, now);
			for (int32 i = 0; i < 32; ++i)
			{
				if (ackBits & (1u << i))
					OnPacketAcked(static_cast<uint16>(ack - 1 - i), now);
			}
			while (!m_ReliableQueue.IsEmpty() && m_ReliableQueue[0].Acked)
				m_ReliableQueue.RemoveAt(0);
			if (!m_HasAck || SequenceGreater(ack, m_HighestAck))
				m_HighestAck = ack;
			m_HasAck = true;
			CountLostPackets();
		}

		// A duplicate, or too old to tell, has nothing new in it.
		if (m_HasReceived)
		{
			if (m_Received[sequence % PacketHistory] == sequence)
				return true;
			if (SequenceLess(sequence, m_RemoteSequence) && static_cast<uint16>(m_RemoteSequence - sequence) >= PacketHistory)
				return true;
		}
		m_Received[sequence % PacketHistory] = sequence;
		if (!m_HasReceived || SequenceGreater(sequence, m_RemoteSequence))
			m_RemoteSequence = sequence;
		m_HasReceived = true;
		m_AckOwed = true;

		for (const Parsed& message : messages)
		{
			auto deliver = [this](NetChannel channel, const uint8* bytes, int32 size) {
				NetMessage& out = m_Delivered.Emplace();
				out.Channel = channel;
				out.Data.Resize(size);
				if (size > 0)
					std::memcpy(out.Data.Data(), bytes, static_cast<usize>(size));
			};

			switch (message.Channel)
			{
				case NetChannel::Unreliable:
					deliver(message.Channel, message.Bytes, message.Size);
					break;
				case NetChannel::Sequenced:
					if (!m_HasSequenced || SequenceGreater(message.Id, m_LastSequenced))
					{
						deliver(message.Channel, message.Bytes, message.Size);
						m_LastSequenced = message.Id;
						m_HasSequenced = true;
					}
					break;
				case NetChannel::Reliable:
				{
					// Already delivered, or too far ahead to hold. The sender resends the latter.
					const uint16 ahead = static_cast<uint16>(message.Id - m_NextExpectedReliable);
					if (ahead >= ReliableWindow)
						break;
					ReceivedReliable& slot = m_ReliableBuffer[message.Id % ReliableWindow];
					if (!slot.Present)
					{
						slot.Present = true;
						slot.Data.Resize(message.Size);
						if (message.Size > 0)
							std::memcpy(slot.Data.Data(), message.Bytes, static_cast<usize>(message.Size));
					}
					// Hand over everything that is now in order.
					for (;;)
					{
						ReceivedReliable& next = m_ReliableBuffer[m_NextExpectedReliable % ReliableWindow];
						if (!next.Present)
							break;
						NetMessage& out = m_Delivered.Emplace();
						out.Channel = NetChannel::Reliable;
						out.Data = std::move(next.Data);
						next.Present = false;
						next.Data = Array<uint8>();
						++m_NextExpectedReliable;
					}
					break;
				}
			}
		}
		return true;
	}

	bool NetConnection::Receive(NetMessage& out)
	{
		if (m_DeliveredHead >= m_Delivered.Num())
			return false;
		out = std::move(m_Delivered[m_DeliveredHead++]);
		if (m_DeliveredHead == m_Delivered.Num())
		{
			m_Delivered.Clear();
			m_DeliveredHead = 0;
		}
		return true;
	}
}
