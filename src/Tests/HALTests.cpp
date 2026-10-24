#include "Tests/TestFramework.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformMisc.h"

#include <cstring>

using namespace FP;

FP_TEST(HAL_PageSizeIsPowerOfTwo)
{
	const usize page = PlatformMemory::GetPageSize();
	FP_EXPECT(page >= 4096);
	FP_EXPECT((page & (page - 1)) == 0);
	FP_EXPECT(PlatformMemory::RoundUpToPage(1) == page);
	FP_EXPECT(PlatformMemory::RoundUpToPage(page + 1) == page * 2);
}

FP_TEST(HAL_AllocatePagesReturnsWholePages)
{
	const usize page = PlatformMemory::GetPageSize();
	MemoryBlock block = PlatformMemory::AllocatePages(100);
	FP_EXPECT(block.Ptr != nullptr);
	FP_EXPECT(block.Size == page);
	std::memset(block.Ptr, 0xAB, block.Size); // the whole page is usable
	PlatformMemory::FreePages(block);
}

FP_TEST(HAL_ReserveThenCommit)
{
	const usize page = PlatformMemory::GetPageSize();
	MemoryBlock range = PlatformMemory::Reserve(page * 16);
	FP_EXPECT(range.Size == page * 16);
	FP_EXPECT(PlatformMemory::Commit(range.Ptr, page * 2));
	std::memset(range.Ptr, 0, page * 2);
	PlatformMemory::Decommit(range.Ptr, page * 2);
	PlatformMemory::Release(range.Ptr, range.Size);
}

FP_TEST(HAL_RandomBytesAreNotAllZero)
{
	uint8 bytes[32] = {};
	PlatformMisc::GetRandomBytes(bytes, sizeof(bytes));
	bool anyNonZero = false;
	for (uint8 b : bytes)
		anyNonZero |= b != 0;
	FP_EXPECT(anyNonZero);
}
