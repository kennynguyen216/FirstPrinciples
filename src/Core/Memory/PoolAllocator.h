#pragma once

#include "Core/Memory/Allocator.h"

namespace FP
{
	// Fixed size slot allocator. Each time it runs out it grabs a run of whole pages and carves
	// all of it into slots, so no part of a page is wasted. Free slots form an intrusive list.
	// Not thread safe.
	class PoolAllocator final : public IAllocator
	{
	public:
		PoolAllocator(usize slotSize, usize slotAlignment = DefaultAlignment);
		~PoolAllocator() override;

		PoolAllocator(const PoolAllocator&) = delete;
		PoolAllocator& operator=(const PoolAllocator&) = delete;

		// size must be <= GetSlotSize(). The returned size is the full slot.
		MemoryBlock Allocate(usize size, usize alignment = DefaultAlignment) override;
		void Free(void* ptr, usize size) override;

		bool Owns(const void* ptr) const;

		usize GetSlotSize() const { return m_SlotSize; }
		usize GetSlotsPerChunk() const { return m_SlotsPerChunk; }
		usize GetNumAllocated() const { return m_NumAllocated; }
		usize GetCapacity() const { return m_NumChunks * m_SlotsPerChunk; }

	private:
		struct FreeSlot
		{
			FreeSlot* Next;
		};

		// Lives at the start of each page run.
		struct ChunkHeader
		{
			ChunkHeader* Next;
		};

		void AddChunk();

		usize m_SlotSize = 0;
		usize m_SlotAlignment = 0;
		usize m_ChunkSize = 0;
		usize m_FirstSlotOffset = 0;
		usize m_SlotsPerChunk = 0;
		usize m_NumChunks = 0;
		usize m_NumAllocated = 0;
		FreeSlot* m_FreeList = nullptr;
		ChunkHeader* m_Chunks = nullptr;
	};
}
