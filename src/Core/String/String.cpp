#include "Core/String/String.h"

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace FP
{
	namespace
	{
		char* AllocChars(int32 capacity, int32& outCapacity)
		{
			MemoryBlock block = GetDefaultAllocator().Allocate(static_cast<usize>(capacity) + 1, 1);
			outCapacity = static_cast<int32>(block.Size) - 1;
			return static_cast<char*>(block.Ptr);
		}

		void FreeChars(char* data, int32 capacity)
		{
			GetDefaultAllocator().Free(data, static_cast<usize>(capacity) + 1);
		}
	}

	String::String(const char* text) : String(std::string_view(text ? text : "")) {}

	String::String(const char* text, int32 length)
	{
		m_Inline[0] = '\0';
		Append(std::string_view(text, static_cast<usize>(length)));
	}

	String::String(String&& other) noexcept
	{
		if (other.IsInline())
		{
			std::memcpy(m_Inline, other.m_Inline, static_cast<usize>(other.m_Length) + 1);
		}
		else
		{
			m_Data = other.m_Data;
			m_Capacity = other.m_Capacity;
			other.m_Data = other.m_Inline;
			other.m_Capacity = InlineCapacity;
		}
		m_Length = other.m_Length;
		other.m_Length = 0;
		other.m_Inline[0] = '\0';
	}

	String::~String()
	{
		if (!IsInline())
			FreeChars(m_Data, m_Capacity);
	}

	String& String::operator=(const String& other)
	{
		if (this != &other)
			*this = other.View();
		return *this;
	}

	String& String::operator=(String&& other) noexcept
	{
		if (this != &other)
		{
			this->~String();
			new (this) String(std::move(other));
		}
		return *this;
	}

	String& String::operator=(std::string_view text)
	{
		// text may point into this string so copy through a temporary when it overlaps.
		if (text.data() >= m_Data && text.data() <= m_Data + m_Length)
			return *this = String(text);
		m_Length = 0;
		m_Data[0] = '\0';
		return Append(text);
	}

	String String::FormatV(const char* fmt, va_list args)
	{
		va_list copy;
		va_copy(copy, args);
		const int needed = std::vsnprintf(nullptr, 0, fmt, copy);
		va_end(copy);

		String result;
		if (needed <= 0)
			return result;
		result.Reserve(needed);
		std::vsnprintf(result.m_Data, static_cast<usize>(needed) + 1, fmt, args);
		result.m_Length = needed;
		return result;
	}

	String String::Format(const char* fmt, ...)
	{
		va_list args;
		va_start(args, fmt);
		String result = FormatV(fmt, args);
		va_end(args);
		return result;
	}

	String& String::Appendf(const char* fmt, ...)
	{
		va_list args;
		va_start(args, fmt);
		String text = FormatV(fmt, args);
		va_end(args);
		return Append(text.View());
	}

	void String::Grow(int32 minCapacity)
	{
		int32 capacity = m_Capacity * 2;
		if (capacity < minCapacity)
			capacity = minCapacity;

		int32 actual = 0;
		char* data = AllocChars(capacity, actual);
		std::memcpy(data, m_Data, static_cast<usize>(m_Length) + 1);
		if (!IsInline())
			FreeChars(m_Data, m_Capacity);
		m_Data = data;
		m_Capacity = actual;
	}

	void String::Reserve(int32 capacity)
	{
		if (capacity > m_Capacity)
			Grow(capacity);
	}

	String& String::Append(std::string_view text)
	{
		if (text.empty())
			return *this;
		const int32 add = static_cast<int32>(text.size());
		if (m_Length + add > m_Capacity)
		{
			// Appending part of ourselves must survive the reallocation.
			if (text.data() >= m_Data && text.data() < m_Data + m_Capacity)
				return Append(String(text).View());
			Grow(m_Length + add);
		}
		std::memcpy(m_Data + m_Length, text.data(), text.size());
		m_Length += add;
		m_Data[m_Length] = '\0';
		return *this;
	}

	void String::Clear()
	{
		m_Length = 0;
		m_Data[0] = '\0';
	}

	int32 String::Replace(std::string_view from, std::string_view to)
	{
		if (from.empty())
			return 0;
		String result;
		int32 count = 0;
		int32 pos = 0;
		for (int32 found = Find(from); found != INDEX_NONE; found = Find(from, pos))
		{
			result.Append(View().substr(static_cast<usize>(pos), static_cast<usize>(found - pos)));
			result.Append(to);
			pos = found + static_cast<int32>(from.size());
			++count;
		}
		if (count)
		{
			result.Append(View().substr(static_cast<usize>(pos)));
			*this = std::move(result);
		}
		return count;
	}

	int32 String::Find(std::string_view text, int32 start) const
	{
		if (start < 0 || start > m_Length)
			return INDEX_NONE;
		const usize pos = View().find(text, static_cast<usize>(start));
		return pos == std::string_view::npos ? INDEX_NONE : static_cast<int32>(pos);
	}

	int32 String::FindLast(char c) const
	{
		const usize pos = View().rfind(c);
		return pos == std::string_view::npos ? INDEX_NONE : static_cast<int32>(pos);
	}

	bool String::StartsWith(std::string_view text) const { return View().starts_with(text); }
	bool String::EndsWith(std::string_view text) const { return View().ends_with(text); }

	bool String::EqualsIgnoreCase(std::string_view text) const
	{
		if (text.size() != static_cast<usize>(m_Length))
			return false;
		for (int32 i = 0; i < m_Length; ++i)
		{
			if (std::tolower(static_cast<unsigned char>(m_Data[i])) != std::tolower(static_cast<unsigned char>(text[i])))
				return false;
		}
		return true;
	}

	String String::Substring(int32 start, int32 count) const
	{
		if (start < 0)
			start = 0;
		if (start >= m_Length)
			return {};
		if (count < 0 || start + count > m_Length)
			count = m_Length - start;
		return String(m_Data + start, count);
	}

	String String::Right(int32 count) const
	{
		return count >= m_Length ? *this : Substring(m_Length - count, count);
	}

	String String::Trimmed() const
	{
		int32 start = 0;
		int32 end = m_Length;
		while (start < end && std::isspace(static_cast<unsigned char>(m_Data[start])))
			++start;
		while (end > start && std::isspace(static_cast<unsigned char>(m_Data[end - 1])))
			--end;
		return String(m_Data + start, end - start);
	}

	String String::ToLower() const
	{
		String result(*this);
		for (int32 i = 0; i < m_Length; ++i)
			result.m_Data[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(m_Data[i])));
		return result;
	}

	String String::ToUpper() const
	{
		String result(*this);
		for (int32 i = 0; i < m_Length; ++i)
			result.m_Data[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(m_Data[i])));
		return result;
	}

	Array<String> String::Split(char separator, bool keepEmpty) const
	{
		Array<String> parts;
		int32 start = 0;
		for (int32 i = 0; i <= m_Length; ++i)
		{
			if (i == m_Length || m_Data[i] == separator)
			{
				if (keepEmpty || i > start)
					parts.Emplace(m_Data + start, i - start);
				start = i + 1;
			}
		}
		return parts;
	}

	bool String::ToInt(int64& out) const
	{
		if (IsEmpty())
			return false;
		char* end = nullptr;
		errno = 0;
		const long long value = std::strtoll(m_Data, &end, 10);
		if (errno != 0 || end != m_Data + m_Length)
			return false;
		out = value;
		return true;
	}

	bool String::ToFloat(double& out) const
	{
		if (IsEmpty())
			return false;
		char* end = nullptr;
		errno = 0;
		const double value = std::strtod(m_Data, &end);
		if (errno != 0 || end != m_Data + m_Length)
			return false;
		out = value;
		return true;
	}

	String operator+(const String& a, std::string_view b)
	{
		String result;
		result.Reserve(a.Length() + static_cast<int32>(b.size()));
		result.Append(a.View()).Append(b);
		return result;
	}

	String operator+(std::string_view a, const String& b)
	{
		String result;
		result.Reserve(static_cast<int32>(a.size()) + b.Length());
		result.Append(a).Append(b.View());
		return result;
	}

	String operator+(const String& a, const String& b) { return a + b.View(); }
	String operator+(const String& a, const char* b) { return a + std::string_view(b ? b : ""); }
}
