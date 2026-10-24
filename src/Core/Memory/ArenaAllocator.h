#pragma once

#include "Core/Memory/Allocator.h"

namespace FP
{
	// Linear bump allocator over one reserved virtual range. Pages are committed only when the
	// bump pointer reaches them, so a big reservation costs nothing until it is used.
	// Individual frees are ignored except for the most recent allocation. Use Reset or markers
	// to free in bulk. Not thread safe.
	class ArenaAllocator final : public IAllocator
	{
	public:
		static constexpr usize DefaultReserveSize = 256ull * 1024 * 1024;

		explicit ArenaAllocator(usize reserveSize = DefaultReserveSize);
		~ArenaAllocator() override;

		ArenaAllocator(const ArenaAllocator&) = delete;
		ArenaAllocator& operator=(const ArenaAllocator&) = delete;

		MemoryBlock Allocate(usize size, usize alignment = DefaultAlignment) override;
		void Free(void* ptr, usize size) override;

		// The most recent allocation grows in place when there is room.
		MemoryBlock Reallocate(MemoryBlock block, usize newSize, usize alignment = DefaultAlignment) override;

		// Drop every allocation. Committed pages are kept for reuse unless decommit is true.
		void Reset(bool decommit = false);

		using Marker = usize;
		Marker GetMarker() const { return m_Offset; }
		void ResetToMarker(Marker marker);

		usize GetUsed() const { return m_Offset; }
		usize GetCommitted() const { return m_Committed; }
		usize GetReserved() const { return m_Reserved; }

	private:
		bool EnsureCommitted(usize end);

		uint8* m_Base = nullptr;
		usize m_Reserved = 0;
		usize m_Committed = 0;
		usize m_Offset = 0;
		usize m_LastOffset = 0;
	};
}
