#pragma once

#include "Core/Assert.h"
#include "Core/Memory/Allocator.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <type_traits>
#include <utility>

namespace FP
{
	constexpr int32 INDEX_NONE = -1;

	// Growable contiguous array. Memory comes from an IAllocator (the default heap unless one is
	// given) and any extra space the allocator hands back becomes capacity.
	template <typename T>
	class Array
	{
	public:
		using ElementType = T;

		Array() = default;
		explicit Array(IAllocator& allocator) : m_Allocator(&allocator) {}

		Array(std::initializer_list<T> items)
		{
			Reserve(static_cast<int32>(items.size()));
			for (const T& item : items)
				new (m_Data + m_Num++) T(item);
		}

		Array(const Array& other) : m_Allocator(other.m_Allocator) { CopyFrom(other); }

		Array(Array&& other) noexcept
			: m_Data(other.m_Data), m_Num(other.m_Num), m_Capacity(other.m_Capacity), m_Allocator(other.m_Allocator)
		{
			other.m_Data = nullptr;
			other.m_Num = 0;
			other.m_Capacity = 0;
		}

		~Array() { FreeAll(); }

		Array& operator=(const Array& other)
		{
			if (this != &other)
			{
				Clear();
				CopyFrom(other);
			}
			return *this;
		}

		Array& operator=(Array&& other) noexcept
		{
			if (this != &other)
			{
				FreeAll();
				m_Data = other.m_Data;
				m_Num = other.m_Num;
				m_Capacity = other.m_Capacity;
				m_Allocator = other.m_Allocator;
				other.m_Data = nullptr;
				other.m_Num = 0;
				other.m_Capacity = 0;
			}
			return *this;
		}

		// ---- Access --------------------------------------------------------

		int32 Num() const { return m_Num; }
		int32 Capacity() const { return m_Capacity; }
		bool IsEmpty() const { return m_Num == 0; }
		bool IsValidIndex(int32 index) const { return index >= 0 && index < m_Num; }

		T* Data() { return m_Data; }
		const T* Data() const { return m_Data; }

		T& operator[](int32 index)
		{
			FP_ASSERT(IsValidIndex(index), "Array index %d out of range (%d)", index, m_Num);
			return m_Data[index];
		}

		const T& operator[](int32 index) const
		{
			FP_ASSERT(IsValidIndex(index), "Array index %d out of range (%d)", index, m_Num);
			return m_Data[index];
		}

		T& First() { return (*this)[0]; }
		const T& First() const { return (*this)[0]; }
		T& Last() { return (*this)[m_Num - 1]; }
		const T& Last() const { return (*this)[m_Num - 1]; }

		T* begin() { return m_Data; }
		T* end() { return m_Data + m_Num; }
		const T* begin() const { return m_Data; }
		const T* end() const { return m_Data + m_Num; }

		// ---- Adding --------------------------------------------------------

		template <typename... Args>
		T& Emplace(Args&&... args)
		{
			if (m_Num == m_Capacity)
				return EmplaceGrow(std::forward<Args>(args)...);
			T* item = new (m_Data + m_Num) T(std::forward<Args>(args)...);
			++m_Num;
			return *item;
		}

		int32 Add(const T& item) { Emplace(item); return m_Num - 1; }
		int32 Add(T&& item) { Emplace(std::move(item)); return m_Num - 1; }

		// Adds only if not already present. Returns the index either way.
		int32 AddUnique(const T& item)
		{
			const int32 index = Find(item);
			return index != INDEX_NONE ? index : Add(item);
		}

		void Append(const Array& other)
		{
			Reserve(m_Num + other.m_Num);
			for (const T& item : other)
				new (m_Data + m_Num++) T(item);
		}

		void Insert(int32 index, T item)
		{
			FP_ASSERT(index >= 0 && index <= m_Num);
			Emplace(std::move(item));
			std::rotate(m_Data + index, m_Data + m_Num - 1, m_Data + m_Num);
		}

		// ---- Removing ------------------------------------------------------

		// Keeps order. O(n).
		void RemoveAt(int32 index)
		{
			FP_ASSERT(IsValidIndex(index));
			std::move(m_Data + index + 1, m_Data + m_Num, m_Data + index);
			m_Data[--m_Num].~T();
		}

		// Moves the last element into the hole. O(1) but changes order.
		void RemoveAtSwap(int32 index)
		{
			FP_ASSERT(IsValidIndex(index));
			if (index != m_Num - 1)
				m_Data[index] = std::move(m_Data[m_Num - 1]);
			m_Data[--m_Num].~T();
		}

		// Removes every match. Returns how many were removed.
		int32 Remove(const T& item)
		{
			return RemoveAll([&](const T& other) { return other == item; });
		}

		template <typename Predicate>
		int32 RemoveAll(Predicate pred)
		{
			T* newEnd = std::remove_if(m_Data, m_Data + m_Num, pred);
			const int32 removed = static_cast<int32>((m_Data + m_Num) - newEnd);
			for (T* it = newEnd; it != m_Data + m_Num; ++it)
				it->~T();
			m_Num -= removed;
			return removed;
		}

		bool RemoveSwap(const T& item)
		{
			const int32 index = Find(item);
			if (index == INDEX_NONE)
				return false;
			RemoveAtSwap(index);
			return true;
		}

		T Pop()
		{
			FP_ASSERT(m_Num > 0);
			T item = std::move(m_Data[m_Num - 1]);
			m_Data[--m_Num].~T();
			return item;
		}

		// Destroys elements but keeps the memory.
		void Clear()
		{
			DestroyRange(0, m_Num);
			m_Num = 0;
		}

		// ---- Searching -----------------------------------------------------

		int32 Find(const T& item) const
		{
			for (int32 i = 0; i < m_Num; ++i)
			{
				if (m_Data[i] == item)
					return i;
			}
			return INDEX_NONE;
		}

		template <typename Predicate>
		int32 FindIf(Predicate pred) const
		{
			for (int32 i = 0; i < m_Num; ++i)
			{
				if (pred(m_Data[i]))
					return i;
			}
			return INDEX_NONE;
		}

		bool Contains(const T& item) const { return Find(item) != INDEX_NONE; }

		// ---- Sizing --------------------------------------------------------

		void Reserve(int32 capacity)
		{
			if (capacity > m_Capacity)
				Reallocate(capacity);
		}

		// Grows with default constructed elements or shrinks by destroying the tail.
		void Resize(int32 num)
		{
			if (num < m_Num)
			{
				DestroyRange(num, m_Num);
			}
			else
			{
				Reserve(num);
				for (int32 i = m_Num; i < num; ++i)
					new (m_Data + i) T();
			}
			m_Num = num;
		}

		void ShrinkToFit()
		{
			if (m_Num == 0)
				FreeAll();
			else if (m_Capacity > m_Num)
				Reallocate(m_Num, true);
		}

		template <typename Compare = std::less<T>>
		void Sort(Compare compare = Compare())
		{
			std::sort(begin(), end(), compare);
		}

		bool operator==(const Array& other) const
		{
			return m_Num == other.m_Num && std::equal(begin(), end(), other.begin());
		}

		IAllocator& GetAllocator() const { return *m_Allocator; }

	private:
		static constexpr bool Relocatable = std::is_trivially_copyable_v<T>;

		int32 GrowCapacity(int32 minCapacity) const
		{
			const int32 grown = m_Capacity ? m_Capacity * 2 : 4;
			return grown > minCapacity ? grown : minCapacity;
		}

		MemoryBlock CurrentBlock() const { return { m_Data, static_cast<usize>(m_Capacity) * sizeof(T) }; }

		void Reallocate(int32 capacity, bool exact = false)
		{
			if (!exact)
				capacity = capacity > m_Capacity ? GrowCapacity(capacity) : capacity;
			const usize bytes = static_cast<usize>(capacity) * sizeof(T);

			MemoryBlock block;
			if constexpr (Relocatable)
			{
				block = m_Allocator->Reallocate(CurrentBlock(), bytes, alignof(T));
			}
			else
			{
				block = m_Allocator->Allocate(bytes, alignof(T));
				T* newData = static_cast<T*>(block.Ptr);
				for (int32 i = 0; i < m_Num; ++i)
				{
					new (newData + i) T(std::move(m_Data[i]));
					m_Data[i].~T();
				}
				if (m_Data)
					m_Allocator->Free(m_Data, CurrentBlock().Size);
			}
			m_Data = static_cast<T*>(block.Ptr);
			m_Capacity = exact ? capacity : static_cast<int32>(block.Size / sizeof(T));
		}

		// Builds the new element before moving the old ones, so args can point into this array.
		template <typename... Args>
		FP_NOINLINE T& EmplaceGrow(Args&&... args)
		{
			const int32 capacity = GrowCapacity(m_Num + 1);
			MemoryBlock block = m_Allocator->Allocate(static_cast<usize>(capacity) * sizeof(T), alignof(T));
			T* newData = static_cast<T*>(block.Ptr);
			new (newData + m_Num) T(std::forward<Args>(args)...);
			for (int32 i = 0; i < m_Num; ++i)
			{
				new (newData + i) T(std::move(m_Data[i]));
				m_Data[i].~T();
			}
			if (m_Data)
				m_Allocator->Free(m_Data, CurrentBlock().Size);
			m_Data = newData;
			m_Capacity = static_cast<int32>(block.Size / sizeof(T));
			return m_Data[m_Num++];
		}

		void DestroyRange(int32 from, int32 to)
		{
			if constexpr (!std::is_trivially_destructible_v<T>)
			{
				for (int32 i = from; i < to; ++i)
					m_Data[i].~T();
			}
		}

		void CopyFrom(const Array& other)
		{
			Reserve(other.m_Num);
			for (int32 i = 0; i < other.m_Num; ++i)
				new (m_Data + i) T(other.m_Data[i]);
			m_Num = other.m_Num;
		}

		void FreeAll()
		{
			Clear();
			if (m_Data)
				m_Allocator->Free(m_Data, CurrentBlock().Size);
			m_Data = nullptr;
			m_Capacity = 0;
		}

		T* m_Data = nullptr;
		int32 m_Num = 0;
		int32 m_Capacity = 0;
		IAllocator* m_Allocator = &GetDefaultAllocator();
	};
}
