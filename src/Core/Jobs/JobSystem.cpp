#include "Core/Jobs/JobSystem.h"
#include "Core/Jobs/FiberPool.h"
#include "Core/Containers/Array.h"
#include "Core/Log.h"

#include <condition_variable>
#include <mutex>
#include <thread>

namespace FP
{
	namespace
	{
		struct Job
		{
			JobFunction Fn;
			JobCounter* Counter = nullptr;
		};

		// Growable ring so popping the oldest job is O(1).
		class JobQueue
		{
		public:
			void Push(Job&& job)
			{
				if (m_Count == m_Items.Num())
					Grow();
				m_Items[(m_Head + m_Count) % m_Items.Num()] = std::move(job);
				++m_Count;
			}

			bool Pop(Job& out)
			{
				if (m_Count == 0)
					return false;
				out = std::move(m_Items[m_Head]);
				m_Head = (m_Head + 1) % m_Items.Num();
				--m_Count;
				return true;
			}

			int32 Num() const { return m_Count; }

		private:
			void Grow()
			{
				Array<Job> bigger;
				bigger.Resize(m_Items.IsEmpty() ? 64 : m_Items.Num() * 2);
				for (int32 i = 0; i < m_Count; ++i)
					bigger[i] = std::move(m_Items[(m_Head + i) % m_Items.Num()]);
				m_Items = std::move(bigger);
				m_Head = 0;
			}

			Array<Job> m_Items;
			int32 m_Head = 0;
			int32 m_Count = 0;
		};

		// What a fiber asked its thread to do once the switch back to the scheduler is complete.
		// Doing it after the switch means no other thread can resume a fiber that is still running.
		enum class Handoff : uint8
		{
			None,
			Finished,
			Waiting,
		};

		struct Waiter
		{
			PooledFiber* Fiber;
			JobCounter* Counter;
			int32 Target;
		};

		struct ThreadState
		{
			PlatformFiber Scheduler;
			PooledFiber* Current = nullptr;
			Handoff Pending = Handoff::None;
			Waiter PendingWait{};
		};

		thread_local ThreadState* t_Thread = nullptr;

		// Fibers can resume on another thread, so never let the compiler reuse a thread local
		// address across a switch. Going through a function that is never inlined forces a reload.
		FP_NOINLINE ThreadState* CurrentThread() { return t_Thread; }
		FP_NOINLINE void SetCurrentThread(ThreadState* state) { t_Thread = state; }
	}

	struct JobSystemInternals
	{
		JobSystemDesc Desc;
		FiberPool Fibers;
		Array<std::thread> Workers;
		ThreadState MainThread;

		std::mutex QueueMutex;
		JobQueue Queues[3];
		Array<Waiter> Waiters;

		// One job slot per fiber, indexed by fiber, so starting a job never allocates.
		Array<Job> FiberJobs;

		std::mutex WakeMutex;
		std::condition_variable Wake;
		std::atomic<int32> Sleeping{ 0 };
		std::atomic<int32> WaiterCount{ 0 };
		std::atomic<bool> Running{ false };
		std::atomic<bool> WarnedFibersExhausted{ false };

		static void FiberMain(void* arg);
		static void WorkerMain(int32 index);
		bool Step(ThreadState& thread);
		void AfterSwitch(ThreadState& thread);
		void WakeWorkers(bool all);
	};

	namespace
	{
		JobSystemInternals* GSystem = nullptr;
	}

	void JobSystemInternals::FiberMain(void* arg)
	{
		PooledFiber* self = static_cast<PooledFiber*>(arg);
		for (;;)
		{
			Job& job = GSystem->FiberJobs[self->Index];
			job.Fn();
			job.Fn.Reset();
			if (job.Counter)
				JobSystem::Decrement(*job.Counter);

			ThreadState* thread = CurrentThread();
			thread->Pending = Handoff::Finished;
			PlatformFibers::Switch(self->Platform, thread->Scheduler);
			// Resumed later with a new job in UserData.
		}
	}

	void JobSystemInternals::WakeWorkers(bool all)
	{
		// Taking the lock to notify is only worth it when someone is actually asleep.
		if (Sleeping.load(std::memory_order_acquire) == 0)
			return;
		std::lock_guard lock(WakeMutex);
		if (all)
			Wake.notify_all();
		else
			Wake.notify_one();
	}

	void JobSystemInternals::AfterSwitch(ThreadState& thread)
	{
		PooledFiber* fiber = thread.Current;
		thread.Current = nullptr;
		const Handoff pending = thread.Pending;
		thread.Pending = Handoff::None;

		if (pending == Handoff::Finished)
		{
			Fibers.Release(fiber);
			return;
		}
		if (pending == Handoff::Waiting)
		{
			std::lock_guard lock(QueueMutex);
			Waiters.Add(thread.PendingWait);
			WaiterCount.fetch_add(1, std::memory_order_release);
		}
		// The counter may have finished while the fiber was parking, so make sure someone looks.
		if (thread.PendingWait.Counter->Get() <= thread.PendingWait.Target)
			WakeWorkers(false);
	}

	bool JobSystemInternals::Step(ThreadState& thread)
	{
		PooledFiber* fiber = nullptr;
		{
			std::lock_guard lock(QueueMutex);

			// Finish started work before taking new jobs.
			const int32 ready = Waiters.FindIf([](const Waiter& w) { return w.Counter->Get() <= w.Target; });
			if (ready != INDEX_NONE)
			{
				fiber = Waiters[ready].Fiber;
				Waiters.RemoveAtSwap(ready);
				WaiterCount.fetch_sub(1, std::memory_order_release);
			}
			else
			{
				for (JobQueue& queue : Queues)
				{
					if (queue.Num() == 0)
						continue;
					fiber = Fibers.Acquire();
					if (!fiber)
					{
						if (!WarnedFibersExhausted.exchange(true))
							FP_LOG_WARN("All %d job fibers are busy. Raise JobSystemDesc::FiberCount.", Fibers.GetCapacity());
						break;
					}
					queue.Pop(FiberJobs[fiber->Index]);
					break;
				}
			}
		}
		if (!fiber)
			return false;

		thread.Current = fiber;
		PlatformFibers::Switch(thread.Scheduler, fiber->Platform);
		AfterSwitch(thread);
		return true;
	}

	void JobSystemInternals::WorkerMain(int32 index)
	{
		ThreadState thread;
		SetCurrentThread(&thread);
		PlatformFibers::ConvertThread(thread.Scheduler);
		(void)index;

		JobSystemInternals& system = *GSystem;
		while (system.Running.load(std::memory_order_acquire))
		{
			if (system.Step(thread))
				continue;
			// Nothing to do. Sleep until a job or a finished counter wakes us, with a timeout as a safety net.
			std::unique_lock lock(system.WakeMutex);
			system.Sleeping.fetch_add(1, std::memory_order_acq_rel);
			system.Wake.wait_for(lock, std::chrono::milliseconds(2));
			system.Sleeping.fetch_sub(1, std::memory_order_acq_rel);
		}

		PlatformFibers::RevertThread(thread.Scheduler);
		SetCurrentThread(nullptr);
	}

	bool JobSystem::Initialize(const JobSystemDesc& desc)
	{
		FP_CHECK(!GSystem, "Job system already initialized");
		GSystem = new JobSystemInternals();
		JobSystemInternals& system = *GSystem;
		system.Desc = desc;
		if (system.Desc.WorkerCount <= 0)
			system.Desc.WorkerCount = std::max(1, static_cast<int32>(std::thread::hardware_concurrency()) - 1);

		system.FiberJobs.Resize(system.Desc.FiberCount);
		if (!system.Fibers.Initialize(system.Desc.FiberCount, system.Desc.FiberStackSize, &JobSystemInternals::FiberMain))
		{
			delete GSystem;
			GSystem = nullptr;
			return false;
		}

		SetCurrentThread(&system.MainThread);
		PlatformFibers::ConvertThread(system.MainThread.Scheduler);

		system.Running = true;
		for (int32 i = 0; i < system.Desc.WorkerCount; ++i)
			system.Workers.Emplace(&JobSystemInternals::WorkerMain, i);

		FP_LOG("Job system: %d workers, %d fibers of %zu KB", system.Desc.WorkerCount, system.Desc.FiberCount, system.Fibers.GetStackSize() / 1024);
		return true;
	}

	void JobSystem::Shutdown()
	{
		if (!GSystem)
			return;
		JobSystemInternals& system = *GSystem;
		system.Running = false;
		system.WakeWorkers(true);
		for (std::thread& worker : system.Workers)
			worker.join();
		system.Workers.Clear();

		PlatformFibers::RevertThread(system.MainThread.Scheduler);
		SetCurrentThread(nullptr);
		system.Fibers.Shutdown();
		delete GSystem;
		GSystem = nullptr;
	}

	bool JobSystem::IsInitialized()
	{
		return GSystem != nullptr;
	}

	void JobSystem::Run(JobFunction job, JobCounter* counter, JobPriority priority)
	{
		if (counter)
			counter->m_Value.fetch_add(1, std::memory_order_acq_rel);

		if (!GSystem)
		{
			// Without the system, jobs run right away on the calling thread.
			job();
			if (counter)
				Decrement(*counter);
			return;
		}
		{
			std::lock_guard lock(GSystem->QueueMutex);
			GSystem->Queues[static_cast<int>(priority)].Push({ std::move(job), counter });
		}
		GSystem->WakeWorkers(false);
	}

	void JobSystem::Decrement(JobCounter& counter)
	{
		counter.m_Value.fetch_sub(1, std::memory_order_acq_rel);
		// Parked fibers may be waiting on this, and sleeping workers would not notice on their own.
		if (GSystem && GSystem->WaiterCount.load(std::memory_order_acquire) > 0)
			GSystem->WakeWorkers(true);
	}

	void JobSystem::Wait(JobCounter& counter, int32 target)
	{
		if (counter.Get() <= target)
			return;

		ThreadState* thread = CurrentThread();
		if (thread && thread->Current)
		{
			// Inside a job. Park this fiber and let the scheduler find other work.
			PooledFiber* self = thread->Current;
			thread->Pending = Handoff::Waiting;
			thread->PendingWait = { self, &counter, target };
			PlatformFibers::Switch(self->Platform, thread->Scheduler);
			return;
		}

		if (!thread)
		{
			// A thread the job system does not know. It can only wait politely.
			while (counter.Get() > target)
				std::this_thread::yield();
			return;
		}

		// The main thread helps until the counter is done.
		while (counter.Get() > target)
		{
			if (!GSystem->Step(*thread))
				std::this_thread::yield();
		}
	}

	int32 JobSystem::GetWorkerCount()
	{
		return GSystem ? GSystem->Desc.WorkerCount : 0;
	}

	bool JobSystem::IsInsideJob()
	{
		const ThreadState* thread = CurrentThread();
		return thread && thread->Current;
	}
}
