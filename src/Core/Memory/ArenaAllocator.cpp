#include "Core/Memory/ArenaAllocator.h"
#include "Core/Assert.h"
#include "Core/Log.h"

namespace FP
{
	ArenaAllocator::ArenaAllocator(usize reserveSize)
	{
		MemoryBlock range = PlatformMemory::Reserve(reserveSize);
		if (!range)
			FP_FATAL("Failed to reserve %zu bytes for arena", reserveSize);
		m_Base = static_cast<uint8*>(range.Ptr);
		m_Reserved = range.Size;
	}

	ArenaAllocator::~ArenaAllocator()
	{
		PlatformMemory::Release(m_Base, m_Reserved);
	}

	bool ArenaAllocator::EnsureCommitted(usize end)
	{
		if (end <= m_Committed)
			return true;
		if (end > m_Reserved)
		{
			FP_LOG_ERROR("Arena out of reserved space (%zu of %zu bytes)", end, m_Reserved);
			return false;
		}

		// Commit whole pages, at least 64 KiB at a time to keep syscalls rare.
		usize newCommitted = PlatformMemory::RoundUpToPage(end);
		const usize minStep = PlatformMemory::RoundUpToPage(64 * 1024);
		if (newCommitted - m_Committed < minStep)
			newCommitted = m_Committed + minStep;
		if (newCommitted > m_Reserved)
			newCommitted = m_Reserved;

		if (!PlatformMemory::Commit(m_Base + m_Committed, newCommitted - m_Committed))
			FP_FATAL("Failed to commit arena memory");
		m_Committed = newCommitted;
		return true;
	}

	MemoryBlock ArenaAllocator::Allocate(usize size, usize alignment)
	{
		FP_ASSERT(IsPowerOfTwo(alignment));
		const usize start = AlignUp(m_Offset, alignment);
		const usize end = start + size;
		if (!EnsureCommitted(end))
			return {};
		m_LastOffset = start;
		m_Offset = end;
		return { m_Base + start, size };
	}

	void ArenaAllocator::Free(void* ptr, usize size)
	{
		// Only the newest allocation can be popped.
		if (ptr == m_Base + m_LastOffset && m_LastOffset + size == m_Offset)
			m_Offset = m_LastOffset;
	}

	MemoryBlock ArenaAllocator::Reallocate(MemoryBlock block, usize newSize, usize alignment)
	{
		const bool isLast = block.Ptr == m_Base + m_LastOffset && m_LastOffset + block.Size == m_Offset;
		if (isLast && IsAligned(block.Ptr, alignment))
		{
			if (!EnsureCommitted(m_LastOffset + newSize))
				return {};
			m_Offset = m_LastOffset + newSize;
			return { block.Ptr, newSize };
		}
		return IAllocator::Reallocate(block, newSize, alignment);
	}

	void ArenaAllocator::Reset(bool decommit)
	{
		m_Offset = 0;
		m_LastOffset = 0;
		if (decommit && m_Committed)
		{
			PlatformMemory::Decommit(m_Base, m_Committed);
			m_Committed = 0;
		}
	}

	void ArenaAllocator::ResetToMarker(Marker marker)
	{
		FP_ASSERT(marker <= m_Offset);
		m_Offset = marker;
		m_LastOffset = marker;
	}
}
