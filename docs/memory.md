# Memory allocation

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

The allocator layer makes allocation size, alignment and lifetime explicit. Different allocation
patterns can use different backing stores without changing the containers that consume them.
Temporary work can be discarded in bulk, repeated fixed-size objects can reuse slots and large
buffers can use whole virtual-memory pages.

## Choosing an allocator

| Allocator | What it does | When to use it |
|---|---|---|
| HeapAllocator | General allocation through the C runtime heap | Mixed sizes and independent lifetimes |
| PageAllocator | Obtains whole pages from the OS | Large buffers and backing storage for other allocators |
| ArenaAllocator | Advances a pointer through a reserved range and commits pages on demand | Temporary work with a shared lifetime |
| PoolAllocator | Carves page runs into fixed-size slots and reuses freed slots | Repeated allocations of one bounded size |
| SizeClassAllocator | Routes small allocations to power-of-two pools and larger ones to pages | Mixed-size small objects, including GC-managed objects |

GetDefaultAllocator returns the process-wide HeapAllocator. Array and HashMap accept an explicit
IAllocator when a different lifetime or storage policy is needed.

```cpp
#include "Core/Containers/Array.h"
#include "Core/Memory/ArenaAllocator.h"

FP::ArenaAllocator scratch;
{
    FP::Array<FP::int32> indices(scratch);
    indices.Reserve(1024);
    indices.Add(42);
    // Consume the temporary values here.
}
scratch.Reset();
```

Destroy containers and any objects with nontrivial destructors before resetting their backing arena.
Reset releases allocation space without running the destructors of objects stored there. The
allocator must outlive every container and allocation that uses it.

## How it works

IAllocator returns a MemoryBlock containing a pointer and the usable allocation size. That size can
exceed the request because of heap size classes, pool slots or page rounding. Containers can use the
additional space as capacity. Free with the matching allocator and allocation size. FP::New and
FP::Delete pair placement construction and destruction with an allocator.

PlatformMemory separates address reservation from page commitment. An arena reserves 256 MiB by
default and commits backing pages only as allocations reach them. Reset keeps committed pages for
reuse unless decommit is requested. Markers allow a temporary region to be rolled back, and the most
recent allocation can grow in place. Ordinary frees do not reclaim arbitrary arena regions.

PoolAllocator maintains an intrusive free list inside unused slots and allocates another page run
when it runs out. SizeClassAllocator uses eight classes from 16 through 2048 bytes and routes larger
requests to page allocations.

HeapAllocator is thread safe. ArenaAllocator, PoolAllocator and SizeClassAllocator are not thread
safe. Give each worker independent storage or synchronize access when sharing those allocators.

Source: [Allocator.h](../src/Core/Memory/Allocator.h),
[ArenaAllocator.h](../src/Core/Memory/ArenaAllocator.h),
[PoolAllocator.h](../src/Core/Memory/PoolAllocator.h),
[SizeClassAllocator.h](../src/Core/Memory/SizeClassAllocator.h),
[PlatformMemory.h](../src/HAL/PlatformMemory.h).

## Checks and related guides

Run `Projects/Sample/Binaries/Debug/Tests Memory` after building Sample. See [core
containers](core.md), [object lifetime](objects.md) and [fiber stacks](fibers.md).
