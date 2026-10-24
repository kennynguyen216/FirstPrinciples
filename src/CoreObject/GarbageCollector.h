#pragma once

#include "CoreObject/Object.h"
#include "Core/Memory/SizeClassAllocator.h"

namespace FP
{
	// Passed to AddReferencedObjects. Marks what it is given as reachable.
	// References to destroyed objects are set to null while collecting.
	class ReferenceCollector
	{
	public:
		template <typename T>
		void Add(T*& object)
		{
			Object* base = object;
			AddImpl(base);
			if (!base)
				object = nullptr;
		}

		template <typename T>
		void Add(Array<T*>& objects)
		{
			for (T*& object : objects)
				Add(object);
			objects.Remove(nullptr);
		}

	private:
		friend class GarbageCollector;
		explicit ReferenceCollector(Array<Object*>& worklist) : m_Worklist(worklist) {}

		void AddImpl(Object*& object);

		Array<Object*>& m_Worklist;
	};

	struct GarbageCollectionStats
	{
		int32 NumAlive = 0;
		int32 NumFreed = 0;
		double Milliseconds = 0;
	};

	// Stop the world mark and sweep collector. Owns every Object and the memory they live in.
	// Objects come from size class pools so allocation and freeing are cheap.
	// Call Collect at a safe point such as the end of a frame. Main thread only.
	class GarbageCollector
	{
	public:
		static GarbageCollector& Get();

		GarbageCollectionStats Collect();

		Object* Allocate(const Class* cls);

		int32 GetNumObjects() const { return m_NumObjects; }

		// Used by WeakObjectPtr. Each slot reuse bumps its serial so stale handles resolve to null.
		uint32 GetSerial(uint32 index) const { return index < static_cast<uint32>(m_Slots.Num()) ? m_Slots[index].Serial : 0; }
		Object* Resolve(uint32 index, uint32 serial) const;

		// Calls fn for every live object of class cls or a subclass.
		template <typename Fn>
		void ForEachObject(const Class* cls, Fn fn) const
		{
			for (const Slot& slot : m_Slots)
			{
				if (slot.Object && slot.Object->IsA(cls))
					fn(slot.Object);
			}
		}

	private:
		struct Slot
		{
			Object* Object = nullptr;
			uint32 Serial = 1;
		};

		GarbageCollector() = default;
		void Free(Object* object);

		Array<Slot> m_Slots;
		Array<uint32> m_FreeSlots;
		Array<Object*> m_Worklist;
		SizeClassAllocator m_Allocator;
		int32 m_NumObjects = 0;
	};
}
