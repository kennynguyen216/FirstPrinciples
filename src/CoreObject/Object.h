#pragma once

#include "CoreObject/Class.h"

namespace FP
{
	class Object;
	class ReferenceCollector;

	Object* NewObject(const Class* cls, Object* outer, String name);

	// Base for garbage collected objects. Create with NewObject, never with new.
	//
	// An object stays alive while it is reachable from a root. Roots are objects marked with
	// AddToRoot (or held by a StrongObjectPtr). Reachability is found by calling
	// AddReferencedObjects, so every Object pointer a class holds must be reported there or it
	// can dangle after a collection. Use WeakObjectPtr for pointers that should not keep things alive.
	class Object
	{
	public:
		Object() = default;
		virtual ~Object() = default;

		Object(const Object&) = delete;
		Object& operator=(const Object&) = delete;

		static const Class* StaticClass();
		virtual const Class* GetClass() const { return StaticClass(); }

		// Report every Object pointer this object holds. Always call Super::AddReferencedObjects.
		virtual void AddReferencedObjects(ReferenceCollector& collector);

		// Called just before the GC frees the object. Other unreachable objects may already be gone.
		virtual void OnDestroy() {}

		const String& GetName() const { return m_Name; }
		void SetName(String name) { m_Name = std::move(name); }

		const Guid& GetGuid() const { return m_Guid; }
		void SetGuid(const Guid& guid) { m_Guid = guid; }

		// The owner of this object. An object keeps its outer alive.
		Object* GetOuter() const { return m_Outer; }

		template <typename T>
		T* GetTypedOuter() const;

		// Roots are never collected. Calls nest so each AddToRoot needs a RemoveFromRoot.
		void AddToRoot();
		void RemoveFromRoot();
		bool IsRooted() const { return m_RootCount > 0; }

		// Marks the object as dead. The next collection frees it and nulls every reported pointer to it.
		// A rooted object stays alive until it is removed from the root set.
		void Destroy();
		bool IsPendingDestroy() const { return m_PendingDestroy; }

		bool IsA(const Class* cls) const { return GetClass()->IsChildOf(cls); }

		template <typename T>
		bool IsA() const { return IsA(T::StaticClass()); }

		uint32 GetObjectIndex() const { return m_Index; }

	private:
		friend class GarbageCollector;
		friend class ReferenceCollector;
		friend Object* NewObject(const Class* cls, Object* outer, String name);

		String m_Name;
		Guid m_Guid;
		Object* m_Outer = nullptr;
		uint32 m_Index = ~0u;
		uint32 m_RootCount = 0;
		bool m_Marked = false;
		bool m_PendingDestroy = false;
	};

	// Null safe checked downcast.
	template <typename T>
	T* Cast(Object* object)
	{
		return object && object->IsA<T>() ? static_cast<T*>(object) : nullptr;
	}

	template <typename T>
	const T* Cast(const Object* object)
	{
		return object && object->IsA<T>() ? static_cast<const T*>(object) : nullptr;
	}

	// Live and not pending destroy.
	inline bool IsValid(const Object* object) { return object && !object->IsPendingDestroy(); }

	template <typename T>
	T* Object::GetTypedOuter() const
	{
		for (Object* outer = m_Outer; outer; outer = outer->m_Outer)
		{
			if (T* typed = Cast<T>(outer))
				return typed;
		}
		return nullptr;
	}

	// Creates an object of cls (which must be T or a subclass). Name defaults to ClassName_N.
	Object* NewObject(const Class* cls, Object* outer = nullptr, String name = {});

	template <typename T>
	T* NewObject(Object* outer = nullptr, String name = {}, const Class* cls = T::StaticClass())
	{
		FP_ASSERT(cls->IsChildOf(T::StaticClass()));
		return static_cast<T*>(NewObject(cls, outer, std::move(name)));
	}
}
