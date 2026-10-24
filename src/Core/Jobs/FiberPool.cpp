#include "Core/Jobs/FiberPool.h"
#include "Core/Log.h"

namespace FP
{
	bool FiberPool::Initialize(int32 count, usize stackSize, PlatformFiber::EntryFn entry)
	{
		Shutdown();
		m_Fibers.Reserve(count);
		m_Free.Reserve(count);
		for (int32 i = 0; i < count; ++i)
		{
			PooledFiber* fiber = new PooledFiber();
			fiber->Index = i;
			if (!PlatformFibers::Create(fiber->Platform, stackSize, entry, fiber))
			{
				FP_LOG_ERROR("Could not create fiber %d of %d", i, count);
				delete fiber;
				Shutdown();
				return false;
			}
			m_Fibers.Add(fiber);
			m_Free.Add(fiber);
		}
		m_StackSize = count > 0 ? m_Fibers[0]->Platform.StackSize : 0;
		return true;
	}

	void FiberPool::Shutdown()
	{
		std::lock_guard lock(m_Mutex);
		for (PooledFiber* fiber : m_Fibers)
		{
			PlatformFibers::Destroy(fiber->Platform);
			delete fiber;
		}
		m_Fibers.Clear();
		m_Free.Clear();
	}

	PooledFiber* FiberPool::Acquire()
	{
		std::lock_guard lock(m_Mutex);
		return m_Free.IsEmpty() ? nullptr : m_Free.Pop();
	}

	void FiberPool::Release(PooledFiber* fiber)
	{
		std::lock_guard lock(m_Mutex);
		fiber->UserData = nullptr;
		m_Free.Add(fiber);
	}

	int32 FiberPool::GetFreeCount()
	{
		std::lock_guard lock(m_Mutex);
		return m_Free.Num();
	}
}
