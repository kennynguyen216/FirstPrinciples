#pragma once

#include "HAL/PlatformMemory.h"
#include "Core/Memory/MemoryUtils.h"

#include <new>
#include <utility>

namespace FP
{
	// Base interface for every allocator. Allocate returns the real usable size, which can be
	// larger than requested, so containers can use the extra space.
	class IAllocator
	{
	public:
		virtual ~IAllocator() = default;

		virtual MemoryBlock Allocate(usize size, usize alignment = DefaultAlignment) = 0;

		// size is the size that was requested or returned by Allocate.
		virtual void Free(void* ptr, usize size) = 0;

		// Default is allocate, copy, free. Allocators that can grow in place override this.
		virtual MemoryBlock Reallocate(MemoryBlock block, usize newSize, usize alignment = DefaultAlignment);
	};

	// Process wide general purpose allocator used when nothing else is given.
	IAllocator& GetDefaultAllocator();

	template <typename T, typename... Args>
	T* New(IAllocator& allocator, Args&&... args)
	{
		MemoryBlock block = allocator.Allocate(sizeof(T), alignof(T));
		return new (block.Ptr) T(std::forward<Args>(args)...);
	}

	template <typename T>
	void Delete(IAllocator& allocator, T* object)
	{
		if (!object)
			return;
		object->~T();
		allocator.Free(object, sizeof(T));
	}
}
