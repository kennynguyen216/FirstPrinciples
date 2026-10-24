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
