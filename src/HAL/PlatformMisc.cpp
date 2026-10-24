#include "HAL/PlatformMisc.h"

#include <chrono>

#if FP_PLATFORM_WINDOWS
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
	#include <bcrypt.h>
	#pragma comment(lib, "bcrypt.lib")
#else
	#include <cstdlib>
	#include <sys/types.h>
	#include <unistd.h>
	#if FP_PLATFORM_MACOS
		#include <sys/sysctl.h>
	#else
		#include <sys/random.h>
	#endif
#endif

namespace FP::PlatformMisc
{
	void GetRandomBytes(void* dest, usize size)
	{
#if FP_PLATFORM_WINDOWS
		BCryptGenRandom(nullptr, static_cast<PUCHAR>(dest), static_cast<ULONG>(size), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
#elif FP_PLATFORM_MACOS
		arc4random_buf(dest, size);
#else
		getrandom(dest, size, 0);
#endif
	}

	double GetTimeSeconds()
	{
		using Clock = std::chrono::steady_clock;
		static const Clock::time_point start = Clock::now();
		return std::chrono::duration<double>(Clock::now() - start).count();
	}

	bool IsDebuggerPresent()
	{
#if FP_PLATFORM_WINDOWS
		return ::IsDebuggerPresent() != 0;
#elif FP_PLATFORM_MACOS
		int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid() };
		struct kinfo_proc info = {};
		size_t size = sizeof(info);
		if (sysctl(mib, 4, &info, &size, nullptr, 0) != 0)
			return false;
		return (info.kp_proc.p_flag & P_TRACED) != 0;
#else
		return false;
#endif
	}

	void DebugOutput(const char* message)
	{
#if FP_PLATFORM_WINDOWS
		::OutputDebugStringA(message);
		::OutputDebugStringA("\n");
#else
		(void)message;
#endif
	}
}
