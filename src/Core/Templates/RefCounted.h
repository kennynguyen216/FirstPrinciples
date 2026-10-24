#pragma once

#include "Core/Assert.h"

#include <atomic>
#include <type_traits>
#include <utility>

namespace FP
{
	// Base for intrusively reference counted objects. The count lives in the object so a raw
	// pointer can always be turned back into a RefPtr. Deleted when the last RefPtr goes away.
	// Counting is atomic so RefPtrs can be shared across threads.
	class RefCounted
	{
	public:
		void AddRef() const { m_RefCount.fetch_add(1, std::memory_order_relaxed); }

		void Release() const
		{
			const uint32 previous = m_RefCount.fetch_sub(1, std::memory_order_acq_rel);
			FP_ASSERT(previous > 0, "Released a RefCounted object too many times");
			if (previous == 1)
				delete this;
		}

		uint32 GetRefCount() const { return m_RefCount.load(std::memory_order_relaxed); }

	protected:
		RefCounted() = default;
		virtual ~RefCounted() = default;

		// Copies start with their own count of zero.
		RefCounted(const RefCounted&) : m_RefCount(0) {}
		RefCounted& operator=(const RefCounted&) { return *this; }

	private:
		mutable std::atomic<uint32> m_RefCount{ 0 };
	};

	// Smart pointer that keeps a RefCounted object alive.
	template <typename T>
	class RefPtr
	{
	public:
		RefPtr() = default;
		RefPtr(std::nullptr_t) {}
		RefPtr(T* ptr) : m_Ptr(ptr) { if (m_Ptr) m_Ptr->AddRef(); }
		RefPtr(const RefPtr& other) : RefPtr(other.m_Ptr) {}
		RefPtr(RefPtr&& other) noexcept : m_Ptr(other.m_Ptr) { other.m_Ptr = nullptr; }

		template <typename U>
			requires std::is_convertible_v<U*, T*>
		RefPtr(const RefPtr<U>& other) : RefPtr(other.Get()) {}

		template <typename U>
			requires std::is_convertible_v<U*, T*>
		RefPtr(RefPtr<U>&& other) noexcept : m_Ptr(other.Detach()) {}

		~RefPtr() { if (m_Ptr) m_Ptr->Release(); }

		RefPtr& operator=(const RefPtr& other)
		{
			RefPtr(other).Swap(*this);
			return *this;
		}

		RefPtr& operator=(RefPtr&& other) noexcept
		{
			RefPtr(std::move(other)).Swap(*this);
			return *this;
		}

		RefPtr& operator=(T* ptr)
		{
			RefPtr(ptr).Swap(*this);
			return *this;
		}

		T* Get() const { return m_Ptr; }
		T* operator->() const { FP_ASSERT(m_Ptr); return m_Ptr; }
		T& operator*() const { FP_ASSERT(m_Ptr); return *m_Ptr; }
		explicit operator bool() const { return m_Ptr != nullptr; }
		bool IsValid() const { return m_Ptr != nullptr; }

		void Reset() { RefPtr().Swap(*this); }

		// Gives up ownership without releasing. The caller now owns one reference.
		T* Detach()
		{
			T* ptr = m_Ptr;
			m_Ptr = nullptr;
			return ptr;
		}

		void Swap(RefPtr& other) { std::swap(m_Ptr, other.m_Ptr); }

		template <typename U>
		bool operator==(const RefPtr<U>& other) const { return m_Ptr == other.Get(); }
		bool operator==(const T* other) const { return m_Ptr == other; }
		bool operator==(std::nullptr_t) const { return m_Ptr == nullptr; }

	private:
		T* m_Ptr = nullptr;
	};

	template <typename T, typename... Args>
	RefPtr<T> MakeRef(Args&&... args)
	{
		return RefPtr<T>(new T(std::forward<Args>(args)...));
	}

	template <typename T>
	uint32 GetTypeHash(const RefPtr<T>& ptr) { return GetTypeHash(ptr.Get()); }
}
