#include "Tests/TestFramework.h"
#include "HAL/PlatformSocket.h"

#include <cstring>

using namespace FP;

FP_TEST(Net_ParsesAddresses)
{
	NetAddress address;
	FP_EXPECT(NetAddress::Parse("127.0.0.1:7777", address));
	FP_EXPECT(address == NetAddress::Loopback(7777));
	FP_EXPECT(address.ToString() == "127.0.0.1:7777");
	FP_EXPECT(NetAddress::Parse("10.0.0.5", address, 4000) && address.Port == 4000);
	FP_EXPECT(NetAddress::Parse("localhost:9000", address) && address.Ip == 0x7F000001);
	FP_EXPECT(!NetAddress::Parse("127.0.0.1:99999", address));
	FP_EXPECT(!NetAddress::Parse("127.0.0.1", address));
}

FP_TEST(Net_SocketSendsOverLoopback)
{
	UdpSocket a;
	UdpSocket b;
	FP_EXPECT(a.Open(0) && b.Open(0));
	FP_EXPECT(a.GetPort() != 0 && a.GetPort() != b.GetPort());

	const char message[] = "hello";
	FP_EXPECT(a.Send(NetAddress::Loopback(b.GetPort()), message, sizeof(message)));

	// Loopback delivery is quick but not instant, so poll briefly.
	char buffer[UdpSocket::MaxDatagram];
	NetAddress from;
	int32 received = -1;
	for (int i = 0; i < 1000 && received < 0; ++i)
		received = b.Receive(from, buffer, sizeof(buffer));
	FP_EXPECT(received == static_cast<int32>(sizeof(message)));
	FP_EXPECT(std::memcmp(buffer, message, sizeof(message)) == 0);
	FP_EXPECT(from.Port == a.GetPort());
	FP_EXPECT(b.Receive(from, buffer, sizeof(buffer)) == -1);
}

#include "Engine/Net/NetBuffer.h"

FP_TEST(Net_BufferRoundTrips)
{
	NetWriter writer;
	writer.WriteU8(7);
	writer.WriteU16(65000);
	writer.WriteU32(0xDEADBEEF);
	writer.WriteU64(1ull << 40);
	writer.WriteFloat(1.5f);
	writer.WriteBool(true);
	writer.WriteVec3({ 1, -2, 3 });
	writer.WriteString("peer");

	NetReader reader(writer.Data(), writer.Size());
	FP_EXPECT(reader.ReadU8() == 7);
	FP_EXPECT(reader.ReadU16() == 65000);
	FP_EXPECT(reader.ReadU32() == 0xDEADBEEF);
	FP_EXPECT(reader.ReadU64() == (1ull << 40));
	FP_EXPECT(reader.ReadFloat() == 1.5f);
	FP_EXPECT(reader.ReadBool());
	const Vec3 v = reader.ReadVec3();
	FP_EXPECT(v.X == 1 && v.Y == -2 && v.Z == 3);
	FP_EXPECT(reader.ReadString() == "peer");
	FP_EXPECT(reader.IsValid() && reader.Remaining() == 0);

	// Reading past the end fails safely instead of overrunning.
	FP_EXPECT(reader.ReadU32() == 0);
	FP_EXPECT(!reader.IsValid());
	NetReader truncated(writer.Data(), 2);
	truncated.ReadU8();
	FP_EXPECT(truncated.ReadU16() == 0 && !truncated.IsValid());
}

FP_TEST(Net_SequencesWrapAround)
{
	FP_EXPECT(SequenceGreater(1, 0));
	FP_EXPECT(!SequenceGreater(0, 1));
	FP_EXPECT(SequenceGreater(0, 65535));
	FP_EXPECT(SequenceGreater(5, 65500));
	FP_EXPECT(!SequenceGreater(65500, 5));
	FP_EXPECT(!SequenceGreater(9, 9));
	FP_EXPECT(SequenceLess(65535, 2));
}

#include "Engine/Net/NetConnection.h"

#include <random>

namespace
{
	// A pretend network that drops, duplicates and delays packets so they also arrive out of order.
	struct LossyLink
	{
		struct InFlight
		{
			double ArriveAt;
			Array<uint8> Bytes;
		};

		std::mt19937 Random{ 1234 };
		float Loss = 0;
		float Duplicate = 0;
		double MinDelay = 0;
		double MaxDelay = 0;
		Array<InFlight> Packets;

		void Push(const NetWriter& packet, double now)
		{
			std::uniform_real_distribution<float> chance(0, 1);
			std::uniform_real_distribution<double> delay(MinDelay, MaxDelay);
			const int copies = chance(Random) < Loss ? 0 : (chance(Random) < Duplicate ? 2 : 1);
			for (int i = 0; i < copies; ++i)
			{
				InFlight& flight = Packets.Emplace();
				flight.ArriveAt = now + delay(Random);
				flight.Bytes.Resize(packet.Size());
				std::memcpy(flight.Bytes.Data(), packet.Data(), static_cast<usize>(packet.Size()));
			}
		}

		void Deliver(NetConnection& to, double now)
		{
			for (int32 i = 0; i < Packets.Num();)
			{
				if (Packets[i].ArriveAt <= now)
				{
					NetReader reader(Packets[i].Bytes.Data(), Packets[i].Bytes.Num());
					FP_EXPECT(to.ReadPacket(reader, now));
					Packets.RemoveAtSwap(i);
				}
				else
				{
					++i;
				}
			}
		}
	};

	// Sends every queued packet from one side into the link.
	void Flush(NetConnection& from, LossyLink& link, double now)
	{
		NetWriter packet;
		while (from.WritePacket(packet, now))
			link.Push(packet, now);
	}

	void SendIndex(NetConnection& connection, NetChannel channel, uint32 index, int32 padding = 0)
	{
		NetWriter message;
		message.WriteU32(index);
		for (int32 i = 0; i < padding; ++i)
			message.WriteU8(static_cast<uint8>(index + i));
		FP_EXPECT(connection.Send(channel, message.Data(), message.Size()));
	}

	uint32 ReadIndex(const NetMessage& message)
	{
		NetReader reader(message.Data.Data(), message.Data.Num());
		return reader.ReadU32();
	}
}

FP_TEST(Net_ReliableSurvivesABadLink)
{
	NetConnection a;
	NetConnection b;
	LossyLink aToB;
	LossyLink bToA;
	for (LossyLink* link : { &aToB, &bToA })
	{
		link->Loss = 0.3f;
		link->Duplicate = 0.1f;
		link->MinDelay = 0.02;
		link->MaxDelay = 0.15;
	}
	bToA.Random.seed(99);

	constexpr uint32 Count = 600;
	uint32 sentA = 0;
	uint32 sentB = 0;
	Array<uint32> gotB;
	Array<uint32> gotA;
	uint32 lastSequenced = 0;
	bool sequencedOrdered = true;
	int32 sequencedCount = 0;
	const double step = 1.0 / 60.0;
	double now = 0;
	for (int frame = 0; frame < 60 * 60 && (gotB.Num() < static_cast<int32>(Count) || gotA.Num() < static_cast<int32>(Count)); ++frame)
	{
		now = frame * step;
		// Bursts of reliable traffic both ways, some big, plus state on the sequenced channel.
		for (int i = 0; i < 5 && sentA < Count; ++i, ++sentA)
			SendIndex(a, NetChannel::Reliable, sentA, sentA % 7 == 0 ? 900 : 0);
		for (int i = 0; i < 3 && sentB < Count; ++i, ++sentB)
			SendIndex(b, NetChannel::Reliable, sentB);
		SendIndex(a, NetChannel::Sequenced, static_cast<uint32>(frame));

		Flush(a, aToB, now);
		Flush(b, bToA, now);
		aToB.Deliver(b, now);
		bToA.Deliver(a, now);

		NetMessage message;
		while (b.Receive(message))
		{
			if (message.Channel == NetChannel::Reliable)
			{
				gotB.Add(ReadIndex(message));
			}
			else
			{
				const uint32 value = ReadIndex(message);
				sequencedOrdered = sequencedOrdered && (sequencedCount == 0 || value > lastSequenced);
				lastSequenced = value;
				++sequencedCount;
			}
		}
		while (a.Receive(message))
			gotA.Add(ReadIndex(message));
	}

	// Every reliable message, once each, in order, both ways.
	FP_EXPECT(gotB.Num() == static_cast<int32>(Count));
	FP_EXPECT(gotA.Num() == static_cast<int32>(Count));
	for (int32 i = 0; i < gotB.Num(); ++i)
		FP_EXPECT(gotB[i] == static_cast<uint32>(i));
	for (int32 i = 0; i < gotA.Num(); ++i)
		FP_EXPECT(gotA[i] == static_cast<uint32>(i));

	// Sequenced state arrives newest only, and most of it gets through.
	FP_EXPECT(sequencedOrdered);
	FP_EXPECT(sequencedCount > 0);

	FP_EXPECT(a.ReliableResends > 0);
	FP_EXPECT(a.GetPacketLoss() > 0.1f && a.GetPacketLoss() < 0.6f);
	FP_EXPECT(a.GetRtt() > 0.02f && a.GetRtt() < 0.5f);
	FP_EXPECT(a.GetPendingReliable() == 0);
}

FP_TEST(Net_SequencesAndIdsWrapAround)
{
	// More packets and reliable messages than 16 bits can count, over a perfect link.
	NetConnection a;
	NetConnection b;
	NetWriter packet;
	uint32 expected = 0;
	bool ordered = true;
	for (uint32 i = 0; i < 70000; ++i)
	{
		const double now = i * 0.001;
		SendIndex(a, NetChannel::Reliable, i);
		while (a.WritePacket(packet, now))
		{
			NetReader reader(packet.Data(), packet.Size());
			b.ReadPacket(reader, now);
		}
		while (b.WritePacket(packet, now))
		{
			NetReader reader(packet.Data(), packet.Size());
			a.ReadPacket(reader, now);
		}
		NetMessage message;
		while (b.Receive(message))
			ordered = ordered && ReadIndex(message) == expected++;
	}
	FP_EXPECT(ordered);
	FP_EXPECT(expected == 70000);
	FP_EXPECT(a.PacketsSent > 65536);
	FP_EXPECT(a.ReliableResends == 0);
}

FP_TEST(Net_RejectsBadPackets)
{
	NetConnection connection;
	const uint8 tooShort[] = { 1, 2, 3 };
	NetReader shortReader(tooShort, sizeof(tooShort));
	FP_EXPECT(!connection.ReadPacket(shortReader, 0));

	// A message that claims more bytes than the packet holds.
	NetWriter lying;
	lying.WriteU16(0);
	lying.WriteU16(0);
	lying.WriteU32(0);
	lying.WriteU8(0);
	lying.WriteU8(static_cast<uint8>(NetChannel::Unreliable));
	lying.WriteU16(500);
	lying.WriteU8(1);
	NetReader lyingReader(lying.Data(), lying.Size());
	FP_EXPECT(!connection.ReadPacket(lyingReader, 0));
	NetMessage message;
	FP_EXPECT(!connection.Receive(message));

	Array<uint8> huge;
	huge.Resize(NetConnection::MaxMessageSize + 1);
	FP_EXPECT(!connection.Send(NetChannel::Reliable, huge.Data(), huge.Num()));
}
