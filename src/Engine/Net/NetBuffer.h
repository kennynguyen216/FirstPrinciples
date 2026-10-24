#pragma once

#include "Core/CoreMinimal.h"
#include "Engine/Math.h"

#include <cstring>

namespace FP
{
	// 16 bit sequence numbers wrap around. a is newer than b when it is ahead by less than half the range.
	inline bool SequenceGreater(uint16 a, uint16 b)
	{
		return static_cast<uint16>(a - b) != 0 && static_cast<uint16>(a - b) < 32768;
	}

	inline bool SequenceLess(uint16 a, uint16 b) { return SequenceGreater(b, a); }

	// Writes values into a growing byte array. Every platform we ship on is little endian,
	// so values are copied as they sit in memory.
	class NetWriter
	{
	public:
		void WriteU8(uint8 value) { WriteRaw(&value, 1); }
		void WriteU16(uint16 value) { WriteRaw(&value, 2); }
		void WriteU32(uint32 value) { WriteRaw(&value, 4); }
		void WriteU64(uint64 value) { WriteRaw(&value, 8); }
		void WriteFloat(float value) { WriteRaw(&value, 4); }
		void WriteBool(bool value) { WriteU8(value ? 1 : 0); }
		void WriteVec3(Vec3 value) { WriteFloat(value.X); WriteFloat(value.Y); WriteFloat(value.Z); }

		// Length as u16, then the bytes. Longer text is cut at 65535 bytes.
		void WriteString(std::string_view text)
		{
			const usize length = text.size() < 65535 ? text.size() : 65535;
			WriteU16(static_cast<uint16>(length));
			WriteRaw(text.data(), length);
		}

		void WriteBytes(const void* data, usize size) { WriteRaw(data, size); }

		// Overwrites a u16 written earlier, for lengths only known once the body is done.
		void PatchU16(int32 offset, uint16 value) { std::memcpy(m_Data.Data() + offset, &value, 2); }

		const uint8* Data() const { return m_Data.Data(); }
		int32 Size() const { return m_Data.Num(); }
		void Clear() { m_Data.Clear(); }

	private:
		void WriteRaw(const void* data, usize size)
		{
			const int32 offset = m_Data.Num();
			m_Data.Resize(offset + static_cast<int32>(size));
			if (size > 0)
				std::memcpy(m_Data.Data() + offset, data, size);
		}

		Array<uint8> m_Data;
	};

	// Reads what NetWriter wrote. Reading past the end marks the reader failed and returns zeros,
	// so a short or malformed packet can never read out of bounds.
	class NetReader
	{
	public:
		NetReader(const void* data, int32 size) : m_Data(static_cast<const uint8*>(data)), m_Size(size) {}

		uint8 ReadU8() { uint8 v = 0; ReadRaw(&v, 1); return v; }
		uint16 ReadU16() { uint16 v = 0; ReadRaw(&v, 2); return v; }
		uint32 ReadU32() { uint32 v = 0; ReadRaw(&v, 4); return v; }
		uint64 ReadU64() { uint64 v = 0; ReadRaw(&v, 8); return v; }
		float ReadFloat() { float v = 0; ReadRaw(&v, 4); return v; }
		bool ReadBool() { return ReadU8() != 0; }
		Vec3 ReadVec3() { const float x = ReadFloat(); const float y = ReadFloat(); return { x, y, ReadFloat() }; }

		String ReadString()
		{
			const uint16 length = ReadU16();
			if (!CanRead(length))
				return {};
			String text(std::string_view(reinterpret_cast<const char*>(m_Data + m_Offset), length));
			m_Offset += length;
			return text;
		}

		// Points at the next size bytes and skips them, or null when there are not that many.
		const uint8* ReadBytes(int32 size)
		{
			if (!CanRead(size))
				return nullptr;
			const uint8* bytes = m_Data + m_Offset;
			m_Offset += size;
			return bytes;
		}

		int32 Remaining() const { return m_Size - m_Offset; }
		bool IsValid() const { return !m_Failed; }

	private:
		bool CanRead(int32 size)
		{
			if (size < 0 || m_Failed || m_Offset + size > m_Size)
			{
				m_Failed = true;
				return false;
			}
			return true;
		}

		void ReadRaw(void* out, int32 size)
		{
			if (CanRead(size))
			{
				std::memcpy(out, m_Data + m_Offset, static_cast<usize>(size));
				m_Offset += size;
			}
		}

		const uint8* m_Data;
		int32 m_Size;
		int32 m_Offset = 0;
		bool m_Failed = false;
	};
}
