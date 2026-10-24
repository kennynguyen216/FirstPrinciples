#pragma once

#include "HAL/Platform.h"

#include <cstring>
#include <string_view>
#include <type_traits>

namespace FP
{
	// FNV-1a over raw bytes. Simple and good enough for hash maps.
	inline uint64 HashBytes64(const void* data, usize size, uint64 seed = 0xcbf29ce484222325ull)
	{
		const uint8* bytes = static_cast<const uint8*>(data);
		uint64 hash = seed;
		for (usize i = 0; i < size; ++i)
		{
			hash ^= bytes[i];
			hash *= 0x100000001b3ull;
		}
		return hash;
	}

	constexpr uint32 FoldHash(uint64 hash) { return static_cast<uint32>(hash ^ (hash >> 32)); }

	inline uint32 HashBytes(const void* data, usize size) { return FoldHash(HashBytes64(data, size)); }

	// Mixes integer bits so nearby values land far apart (murmur3 finalizer).
	constexpr uint32 MixHash(uint64 key)
	{
		key ^= key >> 33;
		key *= 0xff51afd7ed558ccdull;
		key ^= key >> 33;
		key *= 0xc4ceb9fe1a85ec53ull;
		key ^= key >> 33;
		return static_cast<uint32>(key);
	}

	constexpr uint32 HashCombine(uint32 a, uint32 b)
	{
		return a ^ (b + 0x9e3779b9u + (a << 6) + (a >> 2));
	}

	// GetTypeHash is found by overload resolution. Add one next to your own type to use it as a key.
	template <typename T>
		requires std::is_integral_v<T> || std::is_enum_v<T>
	constexpr uint32 GetTypeHash(T value)
	{
		return MixHash(static_cast<uint64>(value));
	}

	template <typename T>
	uint32 GetTypeHash(T* ptr)
	{
		return MixHash(reinterpret_cast<uptr>(ptr));
	}

	inline uint32 GetTypeHash(float value)
	{
		uint32 bits;
		std::memcpy(&bits, &value, sizeof(bits));
		return MixHash(bits);
	}

	inline uint32 GetTypeHash(std::string_view text) { return HashBytes(text.data(), text.size()); }
}
