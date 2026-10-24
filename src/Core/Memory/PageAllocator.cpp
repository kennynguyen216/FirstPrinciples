#include "Core/Memory/PageAllocator.h"
#include "Core/Assert.h"

namespace FP
{
	MemoryBlock PageAllocator::Allocate(usize size, usize alignment)
	{
		FP_ASSERT(alignment <= PlatformMemory::GetPageSize(), "PageAllocator only aligns to the page size");
		(void)alignment;
		MemoryBlock block = PlatformMemory::AllocatePages(size);
		if (!block)
			FP_FATAL("Out of memory allocating %zu bytes of pages", size);
		return block;
	}

	void PageAllocator::Free(void* ptr, usize size)
	{
		PlatformMemory::Release(ptr, PlatformMemory::RoundUpToPage(size));
	}

	PageAllocator& PageAllocator::Get()
	{
		static PageAllocator allocator;
		return allocator;
	}
}
