#pragma once

#include "Core/Memory/PoolAllocator.h"

namespace FP
{
	// Routes small allocations to pools by size (16 bytes up to 2 KiB) and anything bigger to
	// whole pages. Used by the garbage collector for objects. Not thread safe.
	class SizeClassAllocator final : public IAllocator
	{
	public:
		SizeClassAllocator();
		~SizeClassAllocator() override;

		MemoryBlock Allocate(usize size, usize alignment = DefaultAlignment) override;
		void Free(void* ptr, usize size) override;

		static constexpr usize MaxPooledSize = 2048;

	private:
		static constexpr int NumClasses = 8; // 16, 32, 64 ... 2048
		static int ClassIndex(usize size);

		alignas(PoolAllocator) uint8 m_PoolStorage[NumClasses][sizeof(PoolAllocator)];
		PoolAllocator* Pool(int index) { return reinterpret_cast<PoolAllocator*>(m_PoolStorage[index]); }
	};
}
