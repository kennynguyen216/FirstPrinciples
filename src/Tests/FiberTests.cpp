#include "Tests/TestFramework.h"
#include "HAL/PlatformFiber.h"
#include "HAL/PlatformMisc.h"
#include "Core/Log.h"

#include <thread>

using namespace FP;

namespace
{
	struct PingPong
	{
		PlatformFiber Main;
		PlatformFiber Worker;
		int Count = 0;
		int Rounds = 0;
	};

	void PingPongEntry(void* arg)
	{
		PingPong& state = *static_cast<PingPong*>(arg);
		for (;;)
		{
			++state.Count;
			PlatformFibers::Switch(state.Worker, state.Main);
		}
	}

	struct FloatState
	{
		PlatformFiber Main;
		PlatformFiber Worker;
		double Result = 0;
	};

	// Keeps doubles live across switches so lost callee saved float registers would show.
	void FloatEntry(void* arg)
	{
		FloatState& state = *static_cast<FloatState*>(arg);
		volatile double scale = 1.5;
		double a = 1.25 * scale;
		double b = 2.5 * scale;
		for (;;)
		{
			PlatformFibers::Switch(state.Worker, state.Main);
			a += 1.0;
			b *= 1.0001;
			state.Result = a + b;
		}
	}

	struct DeepState
	{
		PlatformFiber Main;
		PlatformFiber Worker;
		int Depth = 0;
	};

	FP_NOINLINE int Recurse(int depth)
	{
		volatile char pad[512] = {};
		pad[0] = static_cast<char>(depth);
		return depth == 0 ? pad[0] : Recurse(depth - 1) + 1;
	}

	void DeepEntry(void* arg)
	{
		DeepState& state = *static_cast<DeepState*>(arg);
		state.Depth = Recurse(200); // roughly 100 KB of stack
		for (;;)
			PlatformFibers::Switch(state.Worker, state.Main);
	}
}

FP_TEST(Fiber_PingPong)
{
	PingPong state;
	PlatformFibers::ConvertThread(state.Main);
	FP_EXPECT(PlatformFibers::Create(state.Worker, 64 * 1024, &PingPongEntry, &state));

	constexpr int Rounds = 200000;
	const double start = PlatformMisc::GetTimeSeconds();
	for (int i = 0; i < Rounds; ++i)
		PlatformFibers::Switch(state.Main, state.Worker);
	const double fiberSeconds = PlatformMisc::GetTimeSeconds() - start;
	FP_EXPECT(state.Count == Rounds);

	// For comparison, the cheapest thing an OS thread can do per round: start and join.
	constexpr int ThreadRounds = 200;
	const double threadStart = PlatformMisc::GetTimeSeconds();
	int threadCount = 0;
	for (int i = 0; i < ThreadRounds; ++i)
		std::thread([&threadCount]() { ++threadCount; }).join();
	const double threadSeconds = PlatformMisc::GetTimeSeconds() - threadStart;
	FP_EXPECT(threadCount == ThreadRounds);

	FP_LOG("  fiber round trip %.1f ns, thread start and join %.1f us",
		fiberSeconds / Rounds * 1e9, threadSeconds / ThreadRounds * 1e6);

	PlatformFibers::Destroy(state.Worker);
	PlatformFibers::RevertThread(state.Main);
}

FP_TEST(Fiber_KeepsFloatRegisters)
{
	FloatState state;
	PlatformFibers::ConvertThread(state.Main);
	FP_EXPECT(PlatformFibers::Create(state.Worker, 64 * 1024, &FloatEntry, &state));
	PlatformFibers::Switch(state.Main, state.Worker);

	double a = 1.875;
	double b = 3.75;
	for (int i = 0; i < 100; ++i)
	{
		volatile double noise = 7.0 * i; // churn float registers on this side too
		(void)noise;
		PlatformFibers::Switch(state.Main, state.Worker);
		a += 1.0;
		b *= 1.0001;
	}
	FP_EXPECT(std::fabs(state.Result - (a + b)) < 1e-9);

	PlatformFibers::Destroy(state.Worker);
	PlatformFibers::RevertThread(state.Main);
}

FP_TEST(Fiber_StackIsWholePagesAndUsable)
{
	DeepState state;
	PlatformFibers::ConvertThread(state.Main);
	FP_EXPECT(PlatformFibers::Create(state.Worker, 150 * 1000, &DeepEntry, &state));
	FP_EXPECT(state.Worker.StackSize % PlatformMemory::GetPageSize() == 0);
	FP_EXPECT(state.Worker.StackSize >= 150 * 1000);
	PlatformFibers::Switch(state.Main, state.Worker);
	FP_EXPECT(state.Depth == 200);
	PlatformFibers::Destroy(state.Worker);
	PlatformFibers::RevertThread(state.Main);
}

#include "Core/Jobs/FiberPool.h"

namespace
{
	PlatformFiber GPoolMain;
	int GPoolRuns = 0;

	void PoolEntry(void* arg)
	{
		PooledFiber* self = static_cast<PooledFiber*>(arg);
		for (;;)
		{
			GPoolRuns += *static_cast<int*>(self->UserData);
			PlatformFibers::Switch(self->Platform, GPoolMain);
		}
	}
}

FP_TEST(Fiber_PoolHandsOutAndTakesBack)
{
	FiberPool pool;
	FP_EXPECT(pool.Initialize(4, 32 * 1024, &PoolEntry));
	FP_EXPECT(pool.GetCapacity() == 4 && pool.GetFreeCount() == 4);
	FP_EXPECT(pool.GetStackSize() % PlatformMemory::GetPageSize() == 0);

	PlatformFibers::ConvertThread(GPoolMain);
	int amount = 3;
	PooledFiber* fibers[4] = {};
	for (PooledFiber*& fiber : fibers)
	{
		fiber = pool.Acquire();
		FP_EXPECT(fiber != nullptr);
		fiber->UserData = &amount;
		PlatformFibers::Switch(GPoolMain, fiber->Platform);
	}
	FP_EXPECT(GPoolRuns == 12);
	FP_EXPECT(pool.Acquire() == nullptr);

	// A released fiber resumes where it left off when acquired again.
	pool.Release(fibers[2]);
	PooledFiber* again = pool.Acquire();
	FP_EXPECT(again == fibers[2]);
	again->UserData = &amount;
	PlatformFibers::Switch(GPoolMain, again->Platform);
	FP_EXPECT(GPoolRuns == 15);
	PlatformFibers::RevertThread(GPoolMain);
}
