#pragma once

#include "HAL/Platform.h"

namespace FP
{
	constexpr usize DefaultAlignment = alignof(std::max_align_t);

	constexpr bool IsPowerOfTwo(usize value) { return value && (value & (value - 1)) == 0; }

	constexpr usize AlignUp(usize value, usize alignment) { return (value + alignment - 1) & ~(alignment - 1); }

	inline void* AlignUp(void* ptr, usize alignment)
	{
		return reinterpret_cast<void*>(AlignUp(reinterpret_cast<uptr>(ptr), alignment));
	}

	inline bool IsAligned(const void* ptr, usize alignment)
	{
		return (reinterpret_cast<uptr>(ptr) & (alignment - 1)) == 0;
	}
}
