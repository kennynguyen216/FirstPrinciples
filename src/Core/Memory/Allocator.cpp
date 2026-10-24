#include "Core/Memory/Allocator.h"
#include "Core/Memory/HeapAllocator.h"

#include <cstring>

namespace FP
{
	MemoryBlock IAllocator::Reallocate(MemoryBlock block, usize newSize, usize alignment)
	{
		if (block.Ptr && newSize <= block.Size)
			return { block.Ptr, block.Size };

		MemoryBlock result = Allocate(newSize, alignment);
		if (block.Ptr)
		{
			std::memcpy(result.Ptr, block.Ptr, block.Size < newSize ? block.Size : newSize);
			Free(block.Ptr, block.Size);
		}
		return result;
	}

	IAllocator& GetDefaultAllocator()
	{
		static HeapAllocator allocator;
		return allocator;
	}
}
