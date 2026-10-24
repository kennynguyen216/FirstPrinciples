#pragma once

#include "Core/Assert.h"
#include "Core/Hash.h"
#include "Core/Memory/Allocator.h"

#include <utility>

namespace FP
{
	// Open addressing hash map with linear probing. Removal shifts later entries back instead of
	// leaving tombstones, so lookups stay fast. Adding or removing invalidates pointers and iterators.
	// Keys need operator== and a GetTypeHash overload.
	template <typename K, typename V>
	class HashMap
	{
	public:
		struct Pair
		{
			K Key;
			V Value;
		};

		HashMap() = default;
		explicit HashMap(IAllocator& allocator) : m_Allocator(&allocator) {}

		HashMap(const HashMap& other) : m_Allocator(other.m_Allocator)
		{
			Reserve(other.m_Num);
			for (const Pair& pair : other)
				Add(pair.Key, pair.Value);
		}

		HashMap(HashMap&& other) noexcept { Swap(other); }

		HashMap& operator=(const HashMap& other)
		{
			if (this != &other)
			{
				HashMap copy(other);
				Swap(copy);
			}
			return *this;
		}

		HashMap& operator=(HashMap&& other) noexcept
		{
			if (this != &other)
			{
				Clear();
				Swap(other);
			}
			return *this;
		}

		~HashMap() { FreeAll(); }

		int32 Num() const { return m_Num; }
		bool IsEmpty() const { return m_Num == 0; }

		V* Find(const K& key)
		{
			const int32 index = FindIndex(key);
			return index == INDEX_NONE_MAP ? nullptr : &m_Pairs[index].Value;
		}

		const V* Find(const K& key) const { return const_cast<HashMap*>(this)->Find(key); }

		bool Contains(const K& key) const { return Find(key) != nullptr; }

		V FindRef(const K& key, V fallback = V()) const
		{
			const V* value = Find(key);
			return value ? *value : fallback;
		}

		// Inserts or overwrites.
		V& Add(const K& key, V value)
		{
			bool added = false;
			V& slot = FindOrAddImpl(key, added);
			if (!added)
				slot = std::move(value);
			else
				new (&slot) V(std::move(value));
			return slot;
		}

		// Returns the existing value or a default constructed one.
		V& FindOrAdd(const K& key)
		{
			bool added = false;
			V& slot = FindOrAddImpl(key, added);
			if (added)
				new (&slot) V();
			return slot;
		}

		V& operator[](const K& key) { return FindOrAdd(key); }

		bool Remove(const K& key)
		{
			const int32 index = FindIndex(key);
			if (index == INDEX_NONE_MAP)
				return false;
			RemoveAtIndex(static_cast<uint32>(index));
			return true;
		}

		void Clear()
		{
			for (uint32 i = 0; i < m_Capacity; ++i)
			{
				if (m_Hashes[i])
				{
					m_Pairs[i].~Pair();
					m_Hashes[i] = 0;
				}
			}
			m_Num = 0;
		}

		void Reserve(int32 num)
		{
			uint32 capacity = 8;
			while (capacity * 3 / 4 < static_cast<uint32>(num))
				capacity *= 2;
			if (capacity > m_Capacity)
				Rehash(capacity);
		}

		// ---- Iteration -----------------------------------------------------

		template <bool Const>
		class IteratorBase
		{
		public:
			using MapType = std::conditional_t<Const, const HashMap, HashMap>;
			using PairType = std::conditional_t<Const, const Pair, Pair>;

			IteratorBase(MapType* map, uint32 index) : m_Map(map), m_Index(index) { SkipEmpty(); }

			PairType& operator*() const { return m_Map->m_Pairs[m_Index]; }
			PairType* operator->() const { return &m_Map->m_Pairs[m_Index]; }
			IteratorBase& operator++() { ++m_Index; SkipEmpty(); return *this; }
			bool operator!=(const IteratorBase& other) const { return m_Index != other.m_Index; }

		private:
			void SkipEmpty()
			{
				while (m_Index < m_Map->m_Capacity && !m_Map->m_Hashes[m_Index])
					++m_Index;
			}

			MapType* m_Map;
			uint32 m_Index;
		};

		using Iterator = IteratorBase<false>;
		using ConstIterator = IteratorBase<true>;

		Iterator begin() { return Iterator(this, 0); }
		Iterator end() { return Iterator(this, m_Capacity); }
		ConstIterator begin() const { return ConstIterator(this, 0); }
		ConstIterator end() const { return ConstIterator(this, m_Capacity); }

	private:
		static constexpr int32 INDEX_NONE_MAP = -1;

		// Zero marks an empty slot so real hashes are never zero.
		static uint32 HashKey(const K& key)
		{
			const uint32 hash = GetTypeHash(key);
			return hash ? hash : 1;
		}

		uint32 Mask() const { return m_Capacity - 1; }

		int32 FindIndex(const K& key) const
		{
			if (m_Num == 0)
				return INDEX_NONE_MAP;
			const uint32 hash = HashKey(key);
			for (uint32 i = hash & Mask();; i = (i + 1) & Mask())
			{
				if (!m_Hashes[i])
					return INDEX_NONE_MAP;
				if (m_Hashes[i] == hash && m_Pairs[i].Key == key)
					return static_cast<int32>(i);
			}
		}

		// Returns the value slot. When added is true the value is raw memory for the caller to construct.
		V& FindOrAddImpl(const K& key, bool& added)
		{
			const int32 existing = FindIndex(key);
			if (existing != INDEX_NONE_MAP)
			{
				added = false;
				return m_Pairs[existing].Value;
			}

			if (static_cast<uint32>(m_Num + 1) > m_Capacity * 3 / 4)
				Rehash(m_Capacity ? m_Capacity * 2 : 8);

			const uint32 hash = HashKey(key);
			uint32 i = hash & Mask();
			while (m_Hashes[i])
				i = (i + 1) & Mask();

			m_Hashes[i] = hash;
			new (&m_Pairs[i].Key) K(key);
			++m_Num;
			added = true;
			return m_Pairs[i].Value;
		}

		void RemoveAtIndex(uint32 hole)
		{
			m_Pairs[hole].~Pair();
			--m_Num;

			// Shift back any entry whose probe chain passes through the hole.
			for (uint32 j = (hole + 1) & Mask(); m_Hashes[j]; j = (j + 1) & Mask())
			{
				const uint32 ideal = m_Hashes[j] & Mask();
				if (((j - ideal) & Mask()) >= ((j - hole) & Mask()))
				{
					new (&m_Pairs[hole]) Pair(std::move(m_Pairs[j]));
					m_Pairs[j].~Pair();
					m_Hashes[hole] = m_Hashes[j];
					hole = j;
				}
			}
			m_Hashes[hole] = 0;
		}

		void Rehash(uint32 newCapacity)
		{
			FP_ASSERT(IsPowerOfTwo(newCapacity));
			uint32* oldHashes = m_Hashes;
			Pair* oldPairs = m_Pairs;
			const uint32 oldCapacity = m_Capacity;

			m_Capacity = newCapacity;
			m_Hashes = static_cast<uint32*>(m_Allocator->Allocate(sizeof(uint32) * newCapacity, alignof(uint32)).Ptr);
			m_Pairs = static_cast<Pair*>(m_Allocator->Allocate(sizeof(Pair) * newCapacity, alignof(Pair)).Ptr);
			std::memset(m_Hashes, 0, sizeof(uint32) * newCapacity);

			for (uint32 i = 0; i < oldCapacity; ++i)
			{
				if (!oldHashes[i])
					continue;
				uint32 j = oldHashes[i] & Mask();
				while (m_Hashes[j])
					j = (j + 1) & Mask();
				m_Hashes[j] = oldHashes[i];
				new (&m_Pairs[j]) Pair(std::move(oldPairs[i]));
				oldPairs[i].~Pair();
			}

			if (oldHashes)
			{
				m_Allocator->Free(oldHashes, sizeof(uint32) * oldCapacity);
				m_Allocator->Free(oldPairs, sizeof(Pair) * oldCapacity);
			}
		}

		void FreeAll()
		{
			Clear();
			if (m_Hashes)
			{
				m_Allocator->Free(m_Hashes, sizeof(uint32) * m_Capacity);
				m_Allocator->Free(m_Pairs, sizeof(Pair) * m_Capacity);
			}
			m_Hashes = nullptr;
			m_Pairs = nullptr;
			m_Capacity = 0;
		}

		void Swap(HashMap& other)
		{
			std::swap(m_Hashes, other.m_Hashes);
			std::swap(m_Pairs, other.m_Pairs);
			std::swap(m_Capacity, other.m_Capacity);
			std::swap(m_Num, other.m_Num);
			std::swap(m_Allocator, other.m_Allocator);
		}

		uint32* m_Hashes = nullptr;
		Pair* m_Pairs = nullptr;
		uint32 m_Capacity = 0;
		int32 m_Num = 0;
		IAllocator* m_Allocator = &GetDefaultAllocator();
	};
}
