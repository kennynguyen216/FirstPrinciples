#include "Tests/TestFramework.h"
#include "Core/Jobs/JobSystem.h"
#include "HAL/PlatformMisc.h"
#include "Core/Log.h"

#include <memory>
#include <thread>

using namespace FP;

namespace
{
	// Starts the job system for one test and always shuts it down.
	struct ScopedJobs
	{
		explicit ScopedJobs(JobSystemDesc desc = {}) { JobSystem::Initialize(desc); }
		~ScopedJobs() { JobSystem::Shutdown(); }
	};
}

FP_TEST(Job_RunsManyAndCounts)
{
	ScopedJobs jobs;
	std::atomic<int32> sum{ 0 };
	JobCounter counter;
	for (int32 i = 1; i <= 10000; ++i)
		JobSystem::Run([&sum, i]() { sum.fetch_add(i, std::memory_order_relaxed); }, &counter);
	JobSystem::Wait(counter);
	FP_EXPECT(counter.IsDone());
	FP_EXPECT(sum.load() == 10000 * 10001 / 2);
}

FP_TEST(Job_NestedWaitsParkFibers)
{
	// Few fibers on purpose. Parents wait on children, which only works if waiting frees the thread.
	JobSystemDesc desc;
	desc.FiberCount = 8;
	ScopedJobs jobs(desc);

	std::atomic<int32> leaves{ 0 };
	JobCounter parents;
	for (int32 p = 0; p < 4; ++p)
	{
		JobSystem::Run([&leaves]() {
			JobCounter children;
			for (int32 c = 0; c < 50; ++c)
				JobSystem::Run([&leaves]() { leaves.fetch_add(1, std::memory_order_relaxed); }, &children);
			JobSystem::Wait(children);
			FP_EXPECT(children.IsDone());
		}, &parents);
	}
	JobSystem::Wait(parents);
	FP_EXPECT(leaves.load() == 200);
}

FP_TEST(Job_WaitParksInsteadOfBlocking)
{
	// One worker and a waiter that never helps. If a waiting job blocked its thread,
	// the child could never run and this would hang.
	JobSystemDesc desc;
	desc.WorkerCount = 1;
	ScopedJobs jobs(desc);

	std::atomic<int32> order{ 0 };
	int32 childSaw = -1;
	int32 parentSaw = -1;
	JobCounter parent;
	JobSystem::Run([&]() {
		JobCounter child;
		JobSystem::Run([&]() { childSaw = order.fetch_add(1); }, &child);
		FP_EXPECT(JobSystem::IsInsideJob());
		JobSystem::Wait(child);
		parentSaw = order.fetch_add(1);
	}, &parent);

	std::thread outsider([&parent]() { JobSystem::Wait(parent); });
	outsider.join();
	FP_EXPECT(childSaw == 0 && parentSaw == 1);
}

FP_TEST(Job_WaitForPartialCounts)
{
	ScopedJobs jobs;
	JobCounter counter;
	std::atomic<bool> release{ false };
	JobSystem::Run([&release]() {
		while (!release.load())
			std::this_thread::yield();
	}, &counter);
	for (int32 i = 0; i < 9; ++i)
		JobSystem::Run([]() {}, &counter);

	// Wait from a thread that does not help, so the blocked job can never land on the waiter.
	std::thread waiter([&counter]() { JobSystem::Wait(counter, 1); });
	waiter.join();
	FP_EXPECT(counter.Get() == 1);
	release = true;
	JobSystem::Wait(counter);
	FP_EXPECT(counter.IsDone());
}

FP_TEST(Job_RunsInlineWithoutTheSystem)
{
	FP_EXPECT(!JobSystem::IsInitialized());
	JobCounter counter;
	int32 ran = 0;
	JobSystem::Run([&ran]() { ++ran; }, &counter);
	FP_EXPECT(ran == 1 && counter.IsDone());
}

FP_TEST(Job_ThroughputComparedToThreads)
{
	ScopedJobs jobs;
	constexpr int32 Count = 20000;
	std::atomic<int32> done{ 0 };

	const double start = PlatformMisc::GetTimeSeconds();
	JobCounter counter;
	for (int32 i = 0; i < Count; ++i)
		JobSystem::Run([&done]() { done.fetch_add(1, std::memory_order_relaxed); }, &counter);
	JobSystem::Wait(counter);
	const double jobSeconds = PlatformMisc::GetTimeSeconds() - start;
	FP_EXPECT(done.load() == Count);

	FP_LOG("  %d jobs in %.2f ms (%.2f us each) on %d workers", Count, jobSeconds * 1000, jobSeconds / Count * 1e6, JobSystem::GetWorkerCount());
}

FP_TEST(Job_ParallelForCoversEveryIndexOnce)
{
	ScopedJobs jobs;
	constexpr int32 Count = 10007; // not a multiple of the batch size
	// Atomics cannot move, so they live in a plain array rather than an Array.
	std::unique_ptr<std::atomic<int32>[]> hits(new std::atomic<int32>[Count]());
	JobSystem::ParallelFor(Count, 64, [&hits](int32 i) { hits[i].fetch_add(1, std::memory_order_relaxed); });
	bool allOnce = true;
	for (int32 i = 0; i < Count; ++i)
		allOnce &= hits[i].load() == 1;
	FP_EXPECT(allOnce);
}

FP_TEST(Job_NestedParallelFor)
{
	ScopedJobs jobs;
	std::atomic<int64> sum{ 0 };
	JobSystem::ParallelFor(16, 1, [&sum](int32 outer) {
		JobSystem::ParallelFor(100, 10, [&sum, outer](int32 inner) { sum.fetch_add(outer * 100 + inner, std::memory_order_relaxed); });
	});
	FP_EXPECT(sum.load() == static_cast<int64>(1600) * 1599 / 2);
}

#include "Engine/Render/ImageDecoder.h"

FP_TEST(Job_DecodesImagesInParallel)
{
	ScopedJobs jobs;
	Array<String> paths = { "Textures/Bricks_BaseColor.png", "Textures/Bricks_Normal.png", "Textures/Glow_Mask.png", "Textures/Missing.png" };
	Array<DecodedImage> images = DecodeImages(paths);
	FP_EXPECT(images.Num() == 4);
	for (int32 i = 0; i < 3; ++i)
	{
		FP_EXPECT(images[i].Pixels != nullptr);
		FP_EXPECT(images[i].Width == 256 && images[i].Height == 256);
		FP_EXPECT(images[i].Path == paths[i]);
	}
	FP_EXPECT(images[3].Pixels == nullptr);
}
