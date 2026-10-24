#pragma once

// Tiny self registering test framework.
// Write FP_TEST(Name) followed by a body that uses FP_EXPECT(condition).
// Run the Tests exe with an optional name filter as the first argument.

#include "HAL/Platform.h"

namespace FP::Test
{
	using TestFn = void (*)();

	struct TestCase
	{
		const char* Name;
		TestFn Fn;
		TestCase* Next;
	};

	struct Registrar
	{
		Registrar(const char* name, TestFn fn);
	};

	void ReportFailure(const char* expr, const char* file, int line);
	int RunAll(const char* filter);
}

#define FP_TEST(name)                                                   \
	static void FPTest_##name();                                        \
	static ::FP::Test::Registrar FPTestRegistrar_##name(#name, &FPTest_##name); \
	static void FPTest_##name()

#define FP_EXPECT(expr) \
	do { if (!(expr)) ::FP::Test::ReportFailure(#expr, __FILE__, __LINE__); } while (0)
