#pragma once

// Tiny self-registering test framework. Add a test with:
//
//   FP_TEST(Array_AddAndIndex) { FP_EXPECT(arr.Num() == 1); }
//
// and run the FPTests executable (optionally with a substring filter as the first argument).

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
