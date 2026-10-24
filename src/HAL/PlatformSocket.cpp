#include "HAL/PlatformSocket.h"

#include <cstdio>
#include <cstring>

#if FP_PLATFORM_WINDOWS
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#pragma comment(lib, "ws2_32.lib")
	using SocketHandle = SOCKET;
#else
	#include <arpa/inet.h>
	#include <cerrno>
	#include <fcntl.h>
	#include <netdb.h>
	#include <netinet/in.h>
	#include <sys/socket.h>
	#include <unistd.h>
	using SocketHandle = int;
#endif

namespace FP
{
	namespace
	{
		// Winsock needs starting once per process before any socket call.
		bool StartSockets()
		{
#if FP_PLATFORM_WINDOWS
			static const bool started = []() {
				WSADATA data;
				return WSAStartup(MAKEWORD(2, 2), &data) == 0;
			}();
			return started;
#else
			return true;
#endif
		}

		SocketHandle ToNative(uint64 handle) { return static_cast<SocketHandle>(handle); }

		sockaddr_in ToSockaddr(const NetAddress& address)
		{
			sockaddr_in out = {};
			out.sin_family = AF_INET;
			out.sin_addr.s_addr = htonl(address.Ip);
			out.sin_port = htons(address.Port);
			return out;
		}

		void CloseNative(SocketHandle handle)
		{
#if FP_PLATFORM_WINDOWS
			closesocket(handle);
#else
			close(handle);
#endif
		}
	}

	bool NetAddress::Parse(const char* text, NetAddress& out, uint16 defaultPort)
	{
		if (!text || !*text || !StartSockets())
			return false;
		std::string host = text;
		uint16 port = defaultPort;
		const usize colon = host.rfind(':');
		if (colon != std::string::npos)
		{
			char* end = nullptr;
			const long value = std::strtol(host.c_str() + colon + 1, &end, 10);
			if (*end != 0 || value <= 0 || value > 65535)
				return false;
			port = static_cast<uint16>(value);
			host.resize(colon);
		}

		in_addr parsed = {};
		if (inet_pton(AF_INET, host.c_str(), &parsed) != 1)
		{
			// A name like localhost goes through the resolver.
			addrinfo hints = {};
			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_DGRAM;
			addrinfo* found = nullptr;
			if (getaddrinfo(host.c_str(), nullptr, &hints, &found) != 0 || !found)
				return false;
			parsed = reinterpret_cast<sockaddr_in*>(found->ai_addr)->sin_addr;
			freeaddrinfo(found);
		}
		out.Ip = ntohl(parsed.s_addr);
		out.Port = port;
		return out.IsValid();
	}

	std::string NetAddress::ToString() const
	{
		char text[32];
		std::snprintf(text, sizeof(text), "%u.%u.%u.%u:%u", (Ip >> 24) & 0xFF, (Ip >> 16) & 0xFF, (Ip >> 8) & 0xFF, Ip & 0xFF, Port);
		return text;
	}

	bool UdpSocket::Open(uint16 port)
	{
		Close();
		if (!StartSockets())
			return false;
		const SocketHandle handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#if FP_PLATFORM_WINDOWS
		if (handle == INVALID_SOCKET)
			return false;
#else
		if (handle < 0)
			return false;
#endif

		sockaddr_in bindAddress = {};
		bindAddress.sin_family = AF_INET;
		bindAddress.sin_addr.s_addr = htonl(INADDR_ANY);
		bindAddress.sin_port = htons(port);
		if (bind(handle, reinterpret_cast<sockaddr*>(&bindAddress), sizeof(bindAddress)) != 0)
		{
			CloseNative(handle);
			return false;
		}

#if FP_PLATFORM_WINDOWS
		u_long nonBlocking = 1;
		const bool configured = ioctlsocket(handle, FIONBIO, &nonBlocking) == 0;
		// Without this an ICMP port unreachable from a gone peer makes the next recvfrom fail.
		BOOL reportReset = FALSE;
		DWORD returned = 0;
		WSAIoctl(handle, _WSAIOW(IOC_VENDOR, 12), &reportReset, sizeof(reportReset), nullptr, 0, &returned, nullptr, nullptr);
#else
		const bool configured = fcntl(handle, F_SETFL, fcntl(handle, F_GETFL, 0) | O_NONBLOCK) == 0;
#endif
		if (!configured)
		{
			CloseNative(handle);
			return false;
		}

		sockaddr_in bound = {};
		socklen_t length = sizeof(bound);
		getsockname(handle, reinterpret_cast<sockaddr*>(&bound), &length);
		m_Port = ntohs(bound.sin_port);
		m_Handle = static_cast<uint64>(handle);
		return true;
	}

	void UdpSocket::Close()
	{
		if (!IsOpen())
			return;
		CloseNative(ToNative(m_Handle));
		m_Handle = InvalidHandle;
		m_Port = 0;
	}

	bool UdpSocket::Send(const NetAddress& to, const void* data, usize size)
	{
		if (!IsOpen())
			return false;
		const sockaddr_in address = ToSockaddr(to);
		const auto sent = sendto(ToNative(m_Handle), static_cast<const char*>(data), static_cast<int>(size), 0, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
		return sent == static_cast<decltype(sent)>(size);
	}

	int32 UdpSocket::Receive(NetAddress& from, void* buffer, usize capacity)
	{
		if (!IsOpen())
			return -1;
		for (;;)
		{
			sockaddr_in address = {};
			socklen_t length = sizeof(address);
			const auto received = recvfrom(ToNative(m_Handle), static_cast<char*>(buffer), static_cast<int>(capacity), 0, reinterpret_cast<sockaddr*>(&address), &length);
			if (received < 0)
			{
#if FP_PLATFORM_WINDOWS
				// Too big for the buffer. Skip it and look at the next one.
				if (WSAGetLastError() == WSAEMSGSIZE)
					continue;
#endif
				return -1;
			}
			from.Ip = ntohl(address.sin_addr.s_addr);
			from.Port = ntohs(address.sin_port);
			return static_cast<int32>(received);
		}
	}
}
