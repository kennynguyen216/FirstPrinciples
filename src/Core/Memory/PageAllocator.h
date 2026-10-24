#pragma once

#include "Core/Memory/Allocator.h"

namespace FP
{
	// Allocates whole pages straight from the OS. The returned size is always a multiple of the
	// page size. Good for big buffers and as the backing store for other allocators.
	class PageAllocator final : public IAllocator
	{
	public:
		MemoryBlock Allocate(usize size, usize alignment = DefaultAlignment) override;
		void Free(void* ptr, usize size) override;

		static PageAllocator& Get();
	};
}
