# Core containers and utilities

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

Core provides the small types used throughout the engine without depending on the editor or game
systems. Its containers share an allocator interface, making storage choices explicit where
allocation lifetime matters.

## Main types

| Type or layer | What it provides |
|---|---|
| Array | A growable contiguous sequence with allocator-backed capacity |
| HashMap | Key/value lookup using open addressing and linear probing |
| String | An owning, null-terminated UTF-8 string with inline storage for short values |
| Guid | Stable identity values used by objects, entities and asset references |
| RefCounted and RefPtr | Intrusive shared resource ownership with atomic reference counts |
| HAL | Platform operations for memory, fibers, sockets and system information |

Include Core/CoreMinimal.h for common core types, or the individual headers when only a small part
is needed. Engine/Engine.h adds gameplay-facing types on top of the core layer.

## How it works

Array stores elements contiguously and uses the usable size returned by its allocator as capacity.
Growing storage can invalidate pointers and references to elements. Its allocator must outlive it.

HashMap uses GetTypeHash and equality to find keys. Removing an entry shifts subsequent entries back
rather than leaving tombstones. Adding or removing entries can invalidate its pointers and
iterators.

String keeps values of up to 23 bytes inside the object and uses allocated storage for longer
values. Length and indexing refer to bytes, so a byte index does not identify a Unicode character in
general. View exposes a non-owning view whose lifetime depends on the owning string.

The HAL separates OS-specific implementations from the higher layers. Shared ownership through
RefPtr is distinct from CoreObject's garbage-collected graph and from an entity's World ownership.
Choose the lifetime model appropriate to the type being used.

Source: [Array.h](../src/Core/Containers/Array.h), [HashMap.h](../src/Core/Containers/HashMap.h),
[String.h](../src/Core/String/String.h), [Guid.h](../src/Core/Misc/Guid.h),
[RefCounted.h](../src/Core/Templates/RefCounted.h).

## Related guides

[Memory allocation](memory.md), [object lifetime](objects.md), [jobs](jobs.md),
[logging](logging.md).
