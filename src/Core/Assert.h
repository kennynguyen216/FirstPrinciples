#pragma once

#include "HAL/Platform.h"

namespace FP::Detail
{
	// Logs the failure. Returns true if the caller should break into the debugger.
	bool ReportAssertFailure(const char* expr, const char* file, int line, const char* fmt = nullptr, ...);
	[[noreturn]] void FatalError(const char* file, int line, const char* fmt, ...);
}

// FP_ASSERT checks in Debug only and is compiled out in Release.
// FP_VERIFY always runs the expression but only checks it in Debug.
// FP_CHECK is always on. Use it for things that must never happen.
// FP_FATAL logs and aborts.
#if FP_BUILD_DEBUG
	#define FP_ASSERT(expr, ...) \
		do { if (!(expr)) { if (::FP::Detail::ReportAssertFailure(#expr, __FILE__, __LINE__, ##__VA_ARGS__)) FP_DEBUGBREAK(); } } while (0)
	#define FP_VERIFY(expr, ...) FP_ASSERT(expr, ##__VA_ARGS__)
#else
	#define FP_ASSERT(expr, ...) do { (void)sizeof(expr); } while (0)
	#define FP_VERIFY(expr, ...) do { (void)(expr); } while (0)
#endif

#define FP_CHECK(expr, ...) \
	do { if (!(expr)) { ::FP::Detail::ReportAssertFailure(#expr, __FILE__, __LINE__, ##__VA_ARGS__); ::FP::Detail::FatalError(__FILE__, __LINE__, "Check failed: %s", #expr); } } while (0)

#define FP_FATAL(...) ::FP::Detail::FatalError(__FILE__, __LINE__, __VA_ARGS__)
