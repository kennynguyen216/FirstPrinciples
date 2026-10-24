#include "Core/Memory/PoolAllocator.h"
#include "Core/Assert.h"

namespace FP
{
	PoolAllocator::PoolAllocator(usize slotSize, usize slotAlignment)
	{
		FP_CHECK(IsPowerOfTwo(slotAlignment));
		m_SlotAlignment = slotAlignment < alignof(FreeSlot) ? alignof(FreeSlot) : slotAlignment;
		m_SlotSize = AlignUp(slotSize < sizeof(FreeSlot) ? sizeof(FreeSlot) : slotSize, m_SlotAlignment);
		m_FirstSlotOffset = AlignUp(sizeof(ChunkHeader), m_SlotAlignment);

		// One page, or enough pages for at least 8 slots when slots are big.
		m_ChunkSize = PlatformMemory::RoundUpToPage(m_FirstSlotOffset + m_SlotSize * 8);
		if (m_ChunkSize < PlatformMemory::GetPageSize())
			m_ChunkSize = PlatformMemory::GetPageSize();
		m_SlotsPerChunk = (m_ChunkSize - m_FirstSlotOffset) / m_SlotSize;
	}

	PoolAllocator::~PoolAllocator()
	{
		ChunkHeader* chunk = m_Chunks;
		while (chunk)
		{
			ChunkHeader* next = chunk->Next;
			PlatformMemory::Release(chunk, m_ChunkSize);
			chunk = next;
		}
	}

	void PoolAllocator::AddChunk()
	{
		MemoryBlock block = PlatformMemory::AllocatePages(m_ChunkSize);
		if (!block)
			FP_FATAL("Out of memory growing pool");
		FP_ASSERT(block.Size == m_ChunkSize);

		ChunkHeader* chunk = static_cast<ChunkHeader*>(block.Ptr);
		chunk->Next = m_Chunks;
		m_Chunks = chunk;
		++m_NumChunks;

		// Push slots in reverse so allocation walks forward through the page.
		uint8* first = static_cast<uint8*>(block.Ptr) + m_FirstSlotOffset;
		for (usize i = m_SlotsPerChunk; i-- > 0;)
		{
			FreeSlot* slot = reinterpret_cast<FreeSlot*>(first + i * m_SlotSize);
			slot->Next = m_FreeList;
			m_FreeList = slot;
		}
	}

	MemoryBlock PoolAllocator::Allocate(usize size, usize alignment)
	{
		FP_ASSERT(size <= m_SlotSize, "Pool slot is %zu bytes, asked for %zu", m_SlotSize, size);
		FP_ASSERT(alignment <= m_SlotAlignment);
		(void)size;
		(void)alignment;

		if (!m_FreeList)
			AddChunk();
		FreeSlot* slot = m_FreeList;
		m_FreeList = slot->Next;
		++m_NumAllocated;
		return { slot, m_SlotSize };
	}

	void PoolAllocator::Free(void* ptr, usize)
	{
		if (!ptr)
			return;
		FP_ASSERT(Owns(ptr));
		FreeSlot* slot = static_cast<FreeSlot*>(ptr);
		slot->Next = m_FreeList;
		m_FreeList = slot;
		--m_NumAllocated;
	}

	bool PoolAllocator::Owns(const void* ptr) const
	{
		const uint8* p = static_cast<const uint8*>(ptr);
		for (const ChunkHeader* chunk = m_Chunks; chunk; chunk = chunk->Next)
		{
			const uint8* begin = reinterpret_cast<const uint8*>(chunk);
			if (p >= begin + m_FirstSlotOffset && p < begin + m_ChunkSize)
				return true;
		}
		return false;
	}
}
