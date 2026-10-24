#pragma once

#include "CoreObject/GarbageCollector.h"

namespace FP
{
	// Non owning pointer that turns null once the object is collected or destroyed.
	// Safe to keep anywhere, including outside Objects.
	template <typename T>
	class WeakObjectPtr
	{
	public:
		WeakObjectPtr() = default;
		WeakObjectPtr(std::nullptr_t) {}
		WeakObjectPtr(const T* object) { *this = object; }

		WeakObjectPtr& operator=(const T* object)
		{
			if (object)
			{
				m_Index = object->GetObjectIndex();
				m_Serial = GarbageCollector::Get().GetSerial(m_Index);
			}
			else
			{
				m_Index = ~0u;
				m_Serial = 0;
			}
			return *this;
		}

		T* Get() const
		{
			Object* object = GarbageCollector::Get().Resolve(m_Index, m_Serial);
			return IsValid(object) ? static_cast<T*>(object) : nullptr;
		}

		T* operator->() const { return Get(); }
		explicit operator bool() const { return Get() != nullptr; }
		bool operator==(const WeakObjectPtr& other) const { return m_Index == other.m_Index && m_Serial == other.m_Serial; }

	private:
		uint32 m_Index = ~0u;
		uint32 m_Serial = 0;
	};

	// Owning handle that roots the object while it exists. Use it to hold Objects from code that
	// is not an Object itself, such as the editor or a system.
	template <typename T>
	class StrongObjectPtr
	{
	public:
		StrongObjectPtr() = default;
		explicit StrongObjectPtr(T* object) : m_Object(object) { if (m_Object) m_Object->AddToRoot(); }
		StrongObjectPtr(const StrongObjectPtr& other) : StrongObjectPtr(other.m_Object) {}
		StrongObjectPtr(StrongObjectPtr&& other) noexcept : m_Object(other.m_Object) { other.m_Object = nullptr; }
		~StrongObjectPtr() { Reset(); }

		StrongObjectPtr& operator=(StrongObjectPtr other)
		{
			std::swap(m_Object, other.m_Object);
			return *this;
		}

		void Reset(T* object = nullptr)
		{
			if (object)
				object->AddToRoot();
			if (m_Object)
				m_Object->RemoveFromRoot();
			m_Object = object;
		}

		T* Get() const { return m_Object; }
		T* operator->() const { return m_Object; }
		explicit operator bool() const { return m_Object != nullptr; }

	private:
		T* m_Object = nullptr;
	};
}
