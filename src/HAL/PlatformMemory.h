#pragma once

#include "HAL/Platform.h"

namespace FP
{
	// A run of memory and its *real* usable size. OS-backed allocators always round up to whole
	// pages and report the full size so callers can use every byte they were given.
	struct MemoryBlock
	{
		void* Ptr = nullptr;
		usize Size = 0;

		explicit operator bool() const { return Ptr != nullptr; }
	};

	namespace PlatformMemory
	{
		// Size of one virtual memory page (4 KiB on x64, 16 KiB on Apple Silicon).
		usize GetPageSize();

		// Granularity at which address space can be reserved (64 KiB on Windows, page size elsewhere).
		usize GetAllocationGranularity();

		usize RoundUpToPage(usize size);

		// Reserve address space without backing it with physical memory.
		MemoryBlock Reserve(usize size);

		// Back [ptr, ptr + size) with physical memory. ptr/size must be page aligned.
		bool Commit(void* ptr, usize size);

		// Return the physical memory for a committed range but keep the address space reserved.
		void Decommit(void* ptr, usize size);

		// Release a range previously returned by Reserve/AllocatePages. size must match the reservation.
		void Release(void* ptr, usize size);

		// Reserve + commit in one go. The returned size is rounded up to whole pages.
		MemoryBlock AllocatePages(usize size);
		void FreePages(MemoryBlock block);
	}
}
