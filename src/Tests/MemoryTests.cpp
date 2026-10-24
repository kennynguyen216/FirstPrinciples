#include "Tests/TestFramework.h"
#include "Core/Memory/ArenaAllocator.h"
#include "Core/Memory/HeapAllocator.h"
#include "Core/Memory/PageAllocator.h"
#include "Core/Memory/PoolAllocator.h"
#include "Core/Memory/SizeClassAllocator.h"

#include <cstring>

using namespace FP;

FP_TEST(Memory_HeapReportsUsableSize)
{
	HeapAllocator heap;
	MemoryBlock block = heap.Allocate(13, 32);
	FP_EXPECT(block.Size >= 13);
	FP_EXPECT(IsAligned(block.Ptr, 32));
	std::memset(block.Ptr, 1, block.Size);
	block = heap.Reallocate(block, 4000);
	FP_EXPECT(block.Size >= 4000);
	heap.Free(block.Ptr, block.Size);
}

FP_TEST(Memory_PageAllocatorGivesWholePages)
{
	const usize page = PlatformMemory::GetPageSize();
	MemoryBlock block = PageAllocator::Get().Allocate(page + 1);
	FP_EXPECT(block.Size == page * 2);
	std::memset(block.Ptr, 0, block.Size);
	PageAllocator::Get().Free(block.Ptr, block.Size);
}

FP_TEST(Memory_ArenaBumpsAndAligns)
{
	ArenaAllocator arena(1024 * 1024);
	FP_EXPECT(arena.GetCommitted() == 0);

	MemoryBlock a = arena.Allocate(3, 1);
	MemoryBlock b = arena.Allocate(8, 64);
	FP_EXPECT(IsAligned(b.Ptr, 64));
	FP_EXPECT(static_cast<uint8*>(b.Ptr) > static_cast<uint8*>(a.Ptr));
	FP_EXPECT(arena.GetCommitted() % PlatformMemory::GetPageSize() == 0);
	FP_EXPECT(arena.GetCommitted() >= arena.GetUsed());
}

FP_TEST(Memory_ArenaCommitsOnDemand)
{
	ArenaAllocator arena(8 * 1024 * 1024);
	MemoryBlock big = arena.Allocate(3 * 1024 * 1024);
	std::memset(big.Ptr, 0xCD, big.Size);
	FP_EXPECT(arena.GetCommitted() >= 3 * 1024 * 1024);
	FP_EXPECT(arena.GetCommitted() < 4 * 1024 * 1024);
}

FP_TEST(Memory_ArenaMarkersAndReset)
{
	ArenaAllocator arena(1024 * 1024);
	arena.Allocate(100);
	auto marker = arena.GetMarker();
	arena.Allocate(500);
	arena.ResetToMarker(marker);
	FP_EXPECT(arena.GetUsed() == marker);
	arena.Reset();
	FP_EXPECT(arena.GetUsed() == 0);
	arena.Reset(true);
	FP_EXPECT(arena.GetCommitted() == 0);
}

FP_TEST(Memory_ArenaGrowsLastAllocationInPlace)
{
	ArenaAllocator arena(1024 * 1024);
	MemoryBlock block = arena.Allocate(64);
	MemoryBlock grown = arena.Reallocate(block, 256);
	FP_EXPECT(grown.Ptr == block.Ptr);
	FP_EXPECT(grown.Size == 256);
	FP_EXPECT(arena.GetUsed() == 256);
}

FP_TEST(Memory_PoolFillsWholePage)
{
	const usize page = PlatformMemory::GetPageSize();
	PoolAllocator pool(48, 16);
	FP_EXPECT(pool.GetSlotSize() == 48);

	MemoryBlock first = pool.Allocate(40);
	FP_EXPECT(first.Size == 48);
	FP_EXPECT(pool.GetCapacity() == pool.GetSlotsPerChunk());
	// Everything after the chunk header is slots, minus less than one slot of tail.
	FP_EXPECT(pool.GetSlotsPerChunk() * 48 + 48 > page - 16);
	FP_EXPECT(pool.Owns(first.Ptr));
	pool.Free(first.Ptr, 40);
	FP_EXPECT(pool.GetNumAllocated() == 0);
}

FP_TEST(Memory_PoolReusesFreedSlots)
{
	PoolAllocator pool(32);
	void* a = pool.Allocate(32).Ptr;
	pool.Free(a, 32);
	void* b = pool.Allocate(32).Ptr;
	FP_EXPECT(a == b);

	const usize perChunk = pool.GetSlotsPerChunk();
	for (usize i = 0; i < perChunk; ++i)
		pool.Allocate(32);
	FP_EXPECT(pool.GetCapacity() == perChunk * 2);
}

FP_TEST(Memory_SizeClassRoutesBySize)
{
	SizeClassAllocator alloc;
	MemoryBlock small = alloc.Allocate(20);
	FP_EXPECT(small.Size == 32);
	MemoryBlock big = alloc.Allocate(5000);
	FP_EXPECT(big.Size % PlatformMemory::GetPageSize() == 0);
	alloc.Free(small.Ptr, 20);
	alloc.Free(big.Ptr, 5000);
}
