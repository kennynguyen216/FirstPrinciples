#pragma once

#include "HAL/Platform.h"

#include <string>

namespace FP
{
	// IPv4 address and port, both in host byte order.
	struct NetAddress
	{
		uint32 Ip = 0;
		uint16 Port = 0;

		static NetAddress Loopback(uint16 port) { return { 0x7F000001, port }; }

		// Accepts "1.2.3.4:5000", "localhost:5000" or a bare "1.2.3.4" with defaultPort.
		static bool Parse(const char* text, NetAddress& out, uint16 defaultPort = 0);

		bool IsValid() const { return Ip != 0 && Port != 0; }
		std::string ToString() const;

		bool operator==(const NetAddress& other) const { return Ip == other.Ip && Port == other.Port; }
		bool operator!=(const NetAddress& other) const { return !(*this == other); }
	};

	// Non blocking UDP socket. Send and Receive never wait.
	class UdpSocket
	{
	public:
		// Biggest datagram worth receiving. Anything we send stays well under it.
		static constexpr usize MaxDatagram = 2048;

		UdpSocket() = default;
		~UdpSocket() { Close(); }

		UdpSocket(const UdpSocket&) = delete;
		UdpSocket& operator=(const UdpSocket&) = delete;

		// Binds to every interface on port. Port 0 lets the OS pick one, see GetPort.
		bool Open(uint16 port);
		void Close();
		bool IsOpen() const { return m_Handle != InvalidHandle; }
		uint16 GetPort() const { return m_Port; }

		bool Send(const NetAddress& to, const void* data, usize size);

		// Bytes read into buffer, or -1 when nothing is waiting. Datagrams bigger than capacity
		// are cut short or skipped depending on the OS, so pass a buffer of at least MaxDatagram.
		int32 Receive(NetAddress& from, void* buffer, usize capacity);

	private:
		static constexpr uint64 InvalidHandle = ~0ull;
		uint64 m_Handle = InvalidHandle;
		uint16 m_Port = 0;
	};
}
