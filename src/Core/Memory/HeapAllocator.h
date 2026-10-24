#pragma once

#include "Core/Memory/Allocator.h"

namespace FP
{
	// General purpose allocator on top of the C runtime heap. Thread safe.
	class HeapAllocator final : public IAllocator
	{
	public:
		MemoryBlock Allocate(usize size, usize alignment = DefaultAlignment) override;
		void Free(void* ptr, usize size) override;
		MemoryBlock Reallocate(MemoryBlock block, usize newSize, usize alignment = DefaultAlignment) override;
	};
}
