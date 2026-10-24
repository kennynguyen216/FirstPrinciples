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
