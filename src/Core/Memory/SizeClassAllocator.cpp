#include "Core/Memory/SizeClassAllocator.h"
#include "Core/Memory/PageAllocator.h"
#include "Core/Assert.h"

#include <bit>

namespace FP
{
	SizeClassAllocator::SizeClassAllocator()
	{
		for (int i = 0; i < NumClasses; ++i)
			new (m_PoolStorage[i]) PoolAllocator(usize(16) << i, 16);
	}

	SizeClassAllocator::~SizeClassAllocator()
	{
		for (int i = 0; i < NumClasses; ++i)
			Pool(i)->~PoolAllocator();
	}

	int SizeClassAllocator::ClassIndex(usize size)
	{
		if (size <= 16)
			return 0;
		return static_cast<int>(std::bit_width(size - 1)) - 4;
	}

	MemoryBlock SizeClassAllocator::Allocate(usize size, usize alignment)
	{
		FP_CHECK(alignment <= 16, "SizeClassAllocator supports up to 16 byte alignment");
		if (size > MaxPooledSize)
			return PageAllocator::Get().Allocate(size, alignment);
		return Pool(ClassIndex(size))->Allocate(size, alignment);
	}

	void SizeClassAllocator::Free(void* ptr, usize size)
	{
		if (!ptr)
			return;
		if (size > MaxPooledSize)
			PageAllocator::Get().Free(ptr, size);
		else
			Pool(ClassIndex(size))->Free(ptr, size);
	}
}
