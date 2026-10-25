# Object lifetime and garbage collection

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

CoreObject manages engine objects that need runtime class information, names, IDs and tracked
lifetimes. World is one of these objects. Its entities are separate EnTT handles owned by that
world, so entity validity and Object lifetime use different mechanisms.

Garbage collection allows reachable object graphs, including cycles, to remain alive without
requiring every relationship to use reference counting.

## Creating and holding objects

Create an Object-derived instance with NewObject rather than ordinary new. Code outside the managed
graph can keep it alive with StrongObjectPtr.

```cpp
#include "Engine/World.h"

FP::StrongObjectPtr<FP::World> world(FP::NewObject<FP::World>());
const FP::Entity entity = world->CreateEntity("Player");
```

StrongObjectPtr adds a root while it holds the object and removes that root when reset or destroyed.
WeakObjectPtr observes an object without retaining it and resolves to null after invalidation. Root
calls nest, so each explicit AddToRoot needs a matching RemoveFromRoot.

An Object-derived class must report every retained Object pointer through AddReferencedObjects and
call its superclass implementation. An unreported raw pointer can dangle after collection. Object
keeps its outer alive through this same reachability mechanism.

## How it works

GarbageCollector performs a main-thread mark-and-sweep collection at a safe point. It traces objects
from roots through reported references, destroys unreachable objects and returns their storage to a
SizeClassAllocator. Class metadata supplies construction, destruction and ancestry information for
NewObject, IsA and Cast.

Destroy marks an object pending destruction. Reported references to destroyed objects are nulled
during collection. A rooted object is retained until its roots are released. Weak handles store an
object slot and serial number, preventing a handle from resolving to a different object after that
slot is reused.

Collection pauses the work that uses the object graph. Do not collect concurrently with workers that
access or mutate those objects. OnDestroy must not assume that other unreachable objects are still
available.

RefCounted and RefPtr provide a separate lifetime model for resources and systems. Their atomic
reference counts permit sharing ownership across threads, but do not make the referenced data itself
safe for concurrent mutation.

Source: [Object.h](../src/CoreObject/Object.h), [ObjectPtr.h](../src/CoreObject/ObjectPtr.h),
[GarbageCollector.h](../src/CoreObject/GarbageCollector.h),
[RefCounted.h](../src/Core/Templates/RefCounted.h).

## Checks and related guides

Run `Tests Object_` and `Tests GC_` using the built Sample test executable. See [worlds and
entities](gameplay.md), [memory allocation](memory.md) and [core utilities](core.md).
