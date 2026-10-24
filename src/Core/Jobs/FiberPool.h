#pragma once

#include "HAL/PlatformFiber.h"
#include "Core/Containers/Array.h"

#include <mutex>

namespace FP
{
	// A fiber owned by a pool. UserData belongs to whoever runs work on it.
	struct PooledFiber
	{
		PlatformFiber Platform;
		int32 Index = 0;
		void* UserData = nullptr;
	};

	// Fixed set of fibers made up front so starting work never allocates a stack.
	// Every fiber runs the same entry function with its PooledFiber as the argument.
	// Acquire and Release are thread safe.
	class FiberPool
	{
	public:
		FiberPool() = default;
		~FiberPool() { Shutdown(); }

		FiberPool(const FiberPool&) = delete;
		FiberPool& operator=(const FiberPool&) = delete;

		bool Initialize(int32 count, usize stackSize, PlatformFiber::EntryFn entry);
		void Shutdown();

		// Null when every fiber is in use.
		PooledFiber* Acquire();
		void Release(PooledFiber* fiber);

		int32 GetCapacity() const { return m_Fibers.Num(); }
		int32 GetFreeCount();
		usize GetStackSize() const { return m_StackSize; }

	private:
		Array<PooledFiber*> m_Fibers;
		Array<PooledFiber*> m_Free;
		std::mutex m_Mutex;
		usize m_StackSize = 0;
	};
}
