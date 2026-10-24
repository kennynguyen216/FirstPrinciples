#include "CoreObject/GarbageCollector.h"
#include "HAL/PlatformMisc.h"

namespace FP
{
	// ---- Object ----------------------------------------------------------------

	namespace
	{
		struct ObjectClassHolder
		{
			Class Value{ "Object", nullptr, sizeof(Object), alignof(Object), &Detail::ConstructObject<Object> };
		};
	}

	const Class* Object::StaticClass()
	{
		static ObjectClassHolder holder;
		return &holder.Value;
	}

	void Object::AddReferencedObjects(ReferenceCollector& collector)
	{
		collector.Add(m_Outer);
	}

	void Object::AddToRoot() { ++m_RootCount; }

	void Object::RemoveFromRoot()
	{
		FP_ASSERT(m_RootCount > 0, "RemoveFromRoot without AddToRoot on %s", m_Name.CStr());
		--m_RootCount;
	}

	void Object::Destroy() { m_PendingDestroy = true; }

	Object* NewObject(const Class* cls, Object* outer, String name)
	{
		FP_CHECK(cls && !cls->IsAbstract(), "Cannot create abstract class %s", cls ? cls->GetName() : "null");
		Object* object = GarbageCollector::Get().Allocate(cls);
		object->m_Outer = outer;
		object->m_Guid = Guid::New();
		object->m_Name = name.IsEmpty() ? String::Format("%s_%u", cls->GetName(), object->m_Index) : std::move(name);
		return object;
	}

	// ---- ReferenceCollector ----------------------------------------------------

	void ReferenceCollector::AddImpl(Object*& object)
	{
		if (!object)
			return;
		if (object->m_PendingDestroy && !object->IsRooted())
		{
			object = nullptr;
			return;
		}
		if (!object->m_Marked)
		{
			object->m_Marked = true;
			m_Worklist.Add(object);
		}
	}

	// ---- GarbageCollector ------------------------------------------------------

	GarbageCollector& GarbageCollector::Get()
	{
		static GarbageCollector collector;
		return collector;
	}

	Object* GarbageCollector::Allocate(const Class* cls)
	{
		MemoryBlock block = m_Allocator.Allocate(cls->GetSize(), cls->GetAlignment());
		Object* object = cls->Construct(block.Ptr);

		uint32 index;
		if (!m_FreeSlots.IsEmpty())
		{
			index = m_FreeSlots.Pop();
		}
		else
		{
			index = static_cast<uint32>(m_Slots.Num());
			m_Slots.Emplace();
		}
		m_Slots[static_cast<int32>(index)].Ptr = object;
		object->m_Index = index;
		++m_NumObjects;
		return object;
	}

	Object* GarbageCollector::Resolve(uint32 index, uint32 serial) const
	{
		if (index >= static_cast<uint32>(m_Slots.Num()))
			return nullptr;
		const Slot& slot = m_Slots[static_cast<int32>(index)];
		return slot.Serial == serial ? slot.Ptr : nullptr;
	}

	void GarbageCollector::Free(Object* object)
	{
		const usize size = object->GetClass()->GetSize();
		Slot& slot = m_Slots[static_cast<int32>(object->m_Index)];
		slot.Ptr = nullptr;
		++slot.Serial;
		m_FreeSlots.Add(object->m_Index);
		--m_NumObjects;

		object->~Object();
		m_Allocator.Free(object, size);
	}

	GarbageCollectionStats GarbageCollector::Collect()
	{
		const double start = PlatformMisc::GetTimeSeconds();

		// Mark from roots.
		m_Worklist.Clear();
		ReferenceCollector collector(m_Worklist);
		for (Slot& slot : m_Slots)
		{
			if (slot.Ptr && slot.Ptr->IsRooted())
			{
				slot.Ptr->m_Marked = true;
				m_Worklist.Add(slot.Ptr);
			}
		}
		while (!m_Worklist.IsEmpty())
			m_Worklist.Pop()->AddReferencedObjects(collector);

		// Sweep in two passes so every OnDestroy runs before any memory is freed.
		Array<Object*> dead;
		for (Slot& slot : m_Slots)
		{
			if (!slot.Ptr)
				continue;
			if (slot.Ptr->m_Marked)
				slot.Ptr->m_Marked = false;
			else
				dead.Add(slot.Ptr);
		}
		for (Object* object : dead)
			object->OnDestroy();
		for (Object* object : dead)
			Free(object);

		GarbageCollectionStats stats;
		stats.NumAlive = m_NumObjects;
		stats.NumFreed = dead.Num();
		stats.Milliseconds = (PlatformMisc::GetTimeSeconds() - start) * 1000.0;
		return stats;
	}
}
