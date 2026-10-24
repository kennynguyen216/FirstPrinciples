#include "Tests/TestFramework.h"
#include "Core/Log.h"

#include <cstring>

namespace FP::Test
{
	namespace
	{
		TestCase* GHead = nullptr;
		TestCase* GTail = nullptr;
		int GFailures = 0;
	}

	Registrar::Registrar(const char* name, TestFn fn)
	{
		// Leaked on purpose: tests live for the whole process.
		TestCase* test = new TestCase{ name, fn, nullptr };
		if (GTail)
			GTail->Next = test;
		else
			GHead = test;
		GTail = test;
	}

	void ReportFailure(const char* expr, const char* file, int line)
	{
		++GFailures;
		FP_LOG_ERROR("  FAILED: %s\n    at %s:%d", expr, file, line);
	}

	int RunAll(const char* filter)
	{
		int run = 0;
		int failedTests = 0;
		for (TestCase* test = GHead; test; test = test->Next)
		{
			if (filter && !std::strstr(test->Name, filter))
				continue;
			const int before = GFailures;
			test->Fn();
			++run;
			const bool passed = GFailures == before;
			failedTests += passed ? 0 : 1;
			FP_LOG("%s %s", passed ? "[ ok ]" : "[FAIL]", test->Name);
		}
		FP_LOG("\n%d tests, %d failed", run, failedTests);
		return failedTests == 0 ? 0 : 1;
	}
}

int main(int argc, char** argv)
{
	return FP::Test::RunAll(argc > 1 ? argv[1] : nullptr);
}
