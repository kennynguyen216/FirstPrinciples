#include "Core/Memory/HeapAllocator.h"
#include "Core/Assert.h"

#include <cstdlib>
#include <cstring>

#if FP_PLATFORM_WINDOWS
	#include <malloc.h>
#elif FP_PLATFORM_MACOS
	#include <malloc/malloc.h>
#endif

namespace FP
{
	namespace
	{
		// Reports how many bytes malloc really gave us so callers can use them.
		usize UsableSize(void* ptr, usize requested, usize alignment)
		{
#if FP_PLATFORM_WINDOWS
			return _aligned_msize(ptr, alignment, 0);
#elif FP_PLATFORM_MACOS
			(void)requested;
			(void)alignment;
			return malloc_size(ptr);
#else
			(void)ptr;
			(void)alignment;
			return requested;
#endif
		}
	}

	MemoryBlock HeapAllocator::Allocate(usize size, usize alignment)
	{
		FP_ASSERT(IsPowerOfTwo(alignment));
		if (size == 0)
			size = 1;
		if (alignment < sizeof(void*))
			alignment = sizeof(void*);

#if FP_PLATFORM_WINDOWS
		void* ptr = _aligned_malloc(size, alignment);
#else
		void* ptr = nullptr;
		if (posix_memalign(&ptr, alignment, size) != 0)
			ptr = nullptr;
#endif
		if (!ptr)
			FP_FATAL("Out of memory allocating %zu bytes", size);
		return { ptr, UsableSize(ptr, size, alignment) };
	}

	void HeapAllocator::Free(void* ptr, usize)
	{
#if FP_PLATFORM_WINDOWS
		_aligned_free(ptr);
#else
		std::free(ptr);
#endif
	}

	MemoryBlock HeapAllocator::Reallocate(MemoryBlock block, usize newSize, usize alignment)
	{
		if (block.Ptr && newSize <= block.Size)
			return block;
#if FP_PLATFORM_WINDOWS
		if (alignment < sizeof(void*))
			alignment = sizeof(void*);
		void* ptr = _aligned_realloc(block.Ptr, newSize, alignment);
		if (!ptr)
			FP_FATAL("Out of memory reallocating %zu bytes", newSize);
		return { ptr, UsableSize(ptr, newSize, alignment) };
#else
		return IAllocator::Reallocate(block, newSize, alignment);
#endif
	}
}
