#pragma once

#include "HAL/Platform.h"

namespace FP::PlatformMisc
{
	// Cryptographically strong random bytes from the OS.
	void GetRandomBytes(void* dest, usize size);

	// Monotonic time in seconds since an arbitrary point.
	double GetTimeSeconds();

	bool IsDebuggerPresent();

	// Write a line to the debugger output (Visual Studio Output window) in addition to stdout.
	void DebugOutput(const char* message);
}
