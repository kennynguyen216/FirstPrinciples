#pragma once

#include "Core/Containers/Array.h"
#include "Core/Hash.h"

#include <cstdarg>
#include <string_view>

#if defined(__GNUC__) || defined(__clang__)
	#define FP_PRINTF_FORMAT(fmtIndex, argIndex) __attribute__((format(printf, fmtIndex, argIndex)))
#else
	#define FP_PRINTF_FORMAT(fmtIndex, argIndex)
#endif

namespace FP
{
	// Owning UTF-8 string. Always null terminated so CStr() can go straight to C APIs.
	// Strings up to InlineCapacity bytes live inside the object with no heap allocation.
	class String
	{
	public:
		static constexpr int32 InlineCapacity = 23;

		String() { m_Inline[0] = '\0'; }
		String(const char* text);
		String(const char* text, int32 length);
		String(std::string_view text) : String(text.data(), static_cast<int32>(text.size())) {}
		String(const String& other) : String(other.View()) {}
		String(String&& other) noexcept;
		~String();

		String& operator=(const String& other);
		String& operator=(String&& other) noexcept;
		String& operator=(std::string_view text);
		String& operator=(const char* text) { return *this = std::string_view(text ? text : ""); }

		static String Format(const char* fmt, ...) FP_PRINTF_FORMAT(1, 2);
		static String FormatV(const char* fmt, va_list args);

		// ---- Access --------------------------------------------------------

		int32 Length() const { return m_Length; }
		bool IsEmpty() const { return m_Length == 0; }
		const char* CStr() const { return m_Data; }
		char* Data() { return m_Data; }
		std::string_view View() const { return { m_Data, static_cast<usize>(m_Length) }; }
		operator std::string_view() const { return View(); }

		char operator[](int32 index) const
		{
			FP_ASSERT(index >= 0 && index < m_Length);
			return m_Data[index];
		}

		const char* begin() const { return m_Data; }
		const char* end() const { return m_Data + m_Length; }

		// ---- Modify --------------------------------------------------------

		String& Append(std::string_view text);
		String& Append(char c) { return Append(std::string_view(&c, 1)); }
		String& Appendf(const char* fmt, ...) FP_PRINTF_FORMAT(2, 3);
		String& operator+=(std::string_view text) { return Append(text); }
		String& operator+=(char c) { return Append(c); }

		void Reserve(int32 capacity);
		void Clear();

		// Replaces every occurrence. Returns how many were replaced.
		int32 Replace(std::string_view from, std::string_view to);

		// ---- Queries -------------------------------------------------------

		int32 Find(std::string_view text, int32 start = 0) const;
		int32 FindLast(char c) const;
		bool Contains(std::string_view text) const { return Find(text) != INDEX_NONE; }
		bool StartsWith(std::string_view text) const;
		bool EndsWith(std::string_view text) const;
		bool EqualsIgnoreCase(std::string_view text) const;

		String Substring(int32 start, int32 count = -1) const;
		String Left(int32 count) const { return Substring(0, count); }
		String Right(int32 count) const;
		String Trimmed() const;
		String ToLower() const;
		String ToUpper() const;
		Array<String> Split(char separator, bool keepEmpty = false) const;

		bool ToInt(int64& out) const;
		bool ToFloat(double& out) const;

		bool operator==(std::string_view other) const { return View() == other; }
		bool operator==(const String& other) const { return View() == other.View(); }
		bool operator==(const char* other) const { return View() == std::string_view(other ? other : ""); }
		auto operator<=>(const String& other) const { return View() <=> other.View(); }

	private:
		bool IsInline() const { return m_Data == m_Inline; }
		void Grow(int32 minCapacity);

		char* m_Data = m_Inline;
		int32 m_Length = 0;
		int32 m_Capacity = InlineCapacity;
		char m_Inline[InlineCapacity + 1];
	};

	String operator+(const String& a, std::string_view b);
	String operator+(std::string_view a, const String& b);
	String operator+(const String& a, const String& b);
	String operator+(const String& a, const char* b);

	inline uint32 GetTypeHash(const String& text) { return GetTypeHash(text.View()); }
}
