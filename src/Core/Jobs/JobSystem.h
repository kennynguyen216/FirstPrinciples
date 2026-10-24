#pragma once

#include "HAL/Platform.h"
#include "Core/Assert.h"

#include <atomic>
#include <new>
#include <type_traits>
#include <utility>

namespace FP
{
	// Counts unfinished jobs. Running jobs with a counter adds to it and each finished job takes
	// one away. Wait on it to block until they are all done.
	class JobCounter
	{
	public:
		JobCounter() = default;
		JobCounter(const JobCounter&) = delete;
		JobCounter& operator=(const JobCounter&) = delete;

		int32 Get() const { return m_Value.load(std::memory_order_acquire); }
		bool IsDone() const { return Get() == 0; }

	private:
		friend class JobSystem;
		std::atomic<int32> m_Value{ 0 };
	};

	enum class JobPriority : uint8
	{
		High,
		Normal,
		Low,
	};

	// A callable stored inside the job itself so queuing a job never allocates.
	// Captures must fit in InlineSize bytes. Capture big things by reference or pointer.
	class JobFunction
	{
	public:
		static constexpr usize InlineSize = 48;

		JobFunction() = default;

		template <typename F, typename = std::enable_if_t<!std::is_same_v<std::decay_t<F>, JobFunction>>>
		JobFunction(F&& fn)
		{
			using Fn = std::decay_t<F>;
			static_assert(sizeof(Fn) <= InlineSize, "Job captures too much. Capture by reference or pointer.");
			static_assert(alignof(Fn) <= alignof(std::max_align_t), "Job capture is over aligned");
			static_assert(std::is_nothrow_move_constructible_v<Fn>, "Job captures must be nothrow movable");
			new (m_Storage) Fn(std::forward<F>(fn));
			m_Invoke = [](void* self) { (*static_cast<Fn*>(self))(); };
			m_Relocate = [](void* dst, void* src) {
				new (dst) Fn(std::move(*static_cast<Fn*>(src)));
				static_cast<Fn*>(src)->~Fn();
			};
			m_Destroy = [](void* self) { static_cast<Fn*>(self)->~Fn(); };
		}

		JobFunction(JobFunction&& other) noexcept { MoveFrom(other); }

		JobFunction& operator=(JobFunction&& other) noexcept
		{
			if (this != &other)
			{
				Reset();
				MoveFrom(other);
			}
			return *this;
		}

		JobFunction(const JobFunction&) = delete;
		JobFunction& operator=(const JobFunction&) = delete;

		~JobFunction() { Reset(); }

		void operator()() { m_Invoke(m_Storage); }
		explicit operator bool() const { return m_Invoke != nullptr; }

		void Reset()
		{
			if (m_Destroy)
				m_Destroy(m_Storage);
			m_Invoke = nullptr;
			m_Relocate = nullptr;
			m_Destroy = nullptr;
		}

	private:
		void MoveFrom(JobFunction& other)
		{
			if (!other.m_Invoke)
				return;
			other.m_Relocate(m_Storage, other.m_Storage);
			m_Invoke = other.m_Invoke;
			m_Relocate = other.m_Relocate;
			m_Destroy = other.m_Destroy;
			other.m_Invoke = nullptr;
			other.m_Relocate = nullptr;
			other.m_Destroy = nullptr;
		}

		alignas(std::max_align_t) uint8 m_Storage[InlineSize];
		void (*m_Invoke)(void*) = nullptr;
		void (*m_Relocate)(void*, void*) = nullptr;
		void (*m_Destroy)(void*) = nullptr;
	};

	struct JobSystemDesc
	{
		// Worker threads. Zero or less means one per core, minus the main thread.
		int32 WorkerCount = 0;

		// Jobs that are running or waiting each hold a fiber, so this caps work in flight.
		int32 FiberCount = 128;

		// Usable stack per fiber, rounded up to whole pages.
		usize FiberStackSize = 128 * 1024;
	};

	// Runs jobs on fibers spread over a pool of worker threads.
	//
	// Each job runs on its own fiber. When a job waits on a counter its fiber is parked and the
	// thread picks up other work, so waiting never blocks a thread. Parked fibers resume, maybe
	// on another thread, once their counter is done. Avoid thread_local inside jobs for that reason.
	//
	// The thread that calls Initialize may also Wait. It helps run jobs while it waits.
	class JobSystem
	{
	public:
		static bool Initialize(const JobSystemDesc& desc = {});

		// Every job must be finished first.
		static void Shutdown();
		static bool IsInitialized();

		// Queues one job. counter, when given, goes up by one now and down by one when the job ends.
		static void Run(JobFunction job, JobCounter* counter = nullptr, JobPriority priority = JobPriority::Normal);

		// Returns when counter is at or below target. Inside a job this parks the fiber.
		// On the thread that initialized the system it runs other jobs until then, so a job must
		// never spin waiting for that thread to do something. It may be the one running the job.
		static void Wait(JobCounter& counter, int32 target = 0);

		static int32 GetWorkerCount();
		static bool IsInsideJob();

	private:
		friend struct JobSystemInternals;
		static void Decrement(JobCounter& counter);
	};
}
