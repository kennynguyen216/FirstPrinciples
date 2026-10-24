#include "HAL/PlatformMemory.h"

#if FP_PLATFORM_WINDOWS
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
#else
	#include <sys/mman.h>
	#include <unistd.h>
#endif

namespace FP::PlatformMemory
{
	namespace
	{
		struct PageInfo
		{
			usize PageSize = 0;
			usize Granularity = 0;

			PageInfo()
			{
#if FP_PLATFORM_WINDOWS
				SYSTEM_INFO info;
				GetSystemInfo(&info);
				PageSize = info.dwPageSize;
				Granularity = info.dwAllocationGranularity;
#else
				PageSize = static_cast<usize>(sysconf(_SC_PAGESIZE));
				Granularity = PageSize;
#endif
			}
		};

		const PageInfo& GetPageInfo()
		{
			static PageInfo info;
			return info;
		}
	}

	usize GetPageSize() { return GetPageInfo().PageSize; }
	usize GetAllocationGranularity() { return GetPageInfo().Granularity; }

	usize RoundUpToPage(usize size)
	{
		const usize page = GetPageSize();
		return (size + page - 1) & ~(page - 1);
	}

	MemoryBlock Reserve(usize size)
	{
		size = RoundUpToPage(size);
#if FP_PLATFORM_WINDOWS
		void* ptr = VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS);
		if (!ptr)
			return {};
#else
		void* ptr = mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
		if (ptr == MAP_FAILED)
			return {};
#endif
		return { ptr, size };
	}

	bool Commit(void* ptr, usize size)
	{
#if FP_PLATFORM_WINDOWS
		return VirtualAlloc(ptr, size, MEM_COMMIT, PAGE_READWRITE) != nullptr;
#else
		return mprotect(ptr, size, PROT_READ | PROT_WRITE) == 0;
#endif
	}

	void Decommit(void* ptr, usize size)
	{
#if FP_PLATFORM_WINDOWS
		VirtualFree(ptr, size, MEM_DECOMMIT);
#else
		// Drop the physical pages, then make the range inaccessible again.
		madvise(ptr, size, MADV_DONTNEED);
		mprotect(ptr, size, PROT_NONE);
#endif
	}

	void Release(void* ptr, usize size)
	{
		if (!ptr)
			return;
#if FP_PLATFORM_WINDOWS
		(void)size;
		VirtualFree(ptr, 0, MEM_RELEASE);
#else
		munmap(ptr, size);
#endif
	}

	MemoryBlock AllocatePages(usize size)
	{
		size = RoundUpToPage(size == 0 ? 1 : size);
#if FP_PLATFORM_WINDOWS
		void* ptr = VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
		if (!ptr)
			return {};
#else
		void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
		if (ptr == MAP_FAILED)
			return {};
#endif
		return { ptr, size };
	}

	void FreePages(MemoryBlock block)
	{
		Release(block.Ptr, block.Size);
	}
}
