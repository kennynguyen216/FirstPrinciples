#include "Engine/Json.h"

#include <cstdlib>

namespace FP
{
	namespace
	{
		const Json& NullJson()
		{
			static const Json null;
			return null;
		}
	}

	// Recursive descent over the text. Depth is limited so hostile input cannot overflow the stack.
	class JsonParser
	{
	public:
		explicit JsonParser(std::string_view text) : m_Text(text) {}

		bool ParseDocument(Json& out)
		{
			if (!ParseValue(out, 0))
				return false;
			SkipSpace();
			return m_Pos == m_Text.size();
		}

	private:
		static constexpr int MaxDepth = 64;

		void SkipSpace()
		{
			while (m_Pos < m_Text.size() && (m_Text[m_Pos] == ' ' || m_Text[m_Pos] == '\t' || m_Text[m_Pos] == '\n' || m_Text[m_Pos] == '\r'))
				++m_Pos;
		}

		bool Consume(std::string_view word)
		{
			if (m_Text.substr(m_Pos, word.size()) != word)
				return false;
			m_Pos += word.size();
			return true;
		}

		bool ParseValue(Json& out, int depth)
		{
			if (depth > MaxDepth)
				return false;
			SkipSpace();
			if (m_Pos >= m_Text.size())
				return false;
			const char c = m_Text[m_Pos];
			if (c == '{')
				return ParseObject(out, depth);
			if (c == '[')
				return ParseArray(out, depth);
			if (c == '"')
			{
				out.m_Type = Json::Type::String;
				return ParseString(out.m_String);
			}
			if (Consume("true"))
			{
				out.m_Type = Json::Type::Bool;
				out.m_Bool = true;
				return true;
			}
			if (Consume("false"))
			{
				out.m_Type = Json::Type::Bool;
				return true;
			}
			if (Consume("null"))
				return true;
			return ParseNumber(out);
		}

		bool ParseNumber(Json& out)
		{
			const std::string text(m_Text.substr(m_Pos, 64));
			char* end = nullptr;
			const double value = std::strtod(text.c_str(), &end);
			if (end == text.c_str())
				return false;
			m_Pos += static_cast<size_t>(end - text.c_str());
			out.m_Type = Json::Type::Number;
			out.m_Number = value;
			return true;
		}

		static void AppendUtf8(std::string& out, uint32 code)
		{
			if (code < 0x80)
				out += static_cast<char>(code);
			else if (code < 0x800)
			{
				out += static_cast<char>(0xC0 | (code >> 6));
				out += static_cast<char>(0x80 | (code & 0x3F));
			}
			else if (code < 0x10000)
			{
				out += static_cast<char>(0xE0 | (code >> 12));
				out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (code & 0x3F));
			}
			else
			{
				out += static_cast<char>(0xF0 | (code >> 18));
				out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
				out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (code & 0x3F));
			}
		}

		bool ParseHex4(uint32& out)
		{
			if (m_Pos + 4 > m_Text.size())
				return false;
			out = 0;
			for (int i = 0; i < 4; ++i)
			{
				const char c = m_Text[m_Pos++];
				out <<= 4;
				if (c >= '0' && c <= '9') out |= static_cast<uint32>(c - '0');
				else if (c >= 'a' && c <= 'f') out |= static_cast<uint32>(c - 'a' + 10);
				else if (c >= 'A' && c <= 'F') out |= static_cast<uint32>(c - 'A' + 10);
				else return false;
			}
			return true;
		}

		bool ParseString(std::string& out)
		{
			++m_Pos; // opening quote
			while (m_Pos < m_Text.size())
			{
				const char c = m_Text[m_Pos++];
				if (c == '"')
					return true;
				if (c != '\\')
				{
					out += c;
					continue;
				}
				if (m_Pos >= m_Text.size())
					return false;
				const char escape = m_Text[m_Pos++];
				switch (escape)
				{
					case '"': out += '"'; break;
					case '\\': out += '\\'; break;
					case '/': out += '/'; break;
					case 'b': out += '\b'; break;
					case 'f': out += '\f'; break;
					case 'n': out += '\n'; break;
					case 'r': out += '\r'; break;
					case 't': out += '\t'; break;
					case 'u':
					{
						uint32 code = 0;
						if (!ParseHex4(code))
							return false;
						// A surrogate pair makes one character past the first 64K.
						if (code >= 0xD800 && code <= 0xDBFF && Consume("\\u"))
						{
							uint32 low = 0;
							if (!ParseHex4(low))
								return false;
							code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
						}
						AppendUtf8(out, code);
						break;
					}
					default: return false;
				}
			}
			return false;
		}

		bool ParseArray(Json& out, int depth)
		{
			++m_Pos;
			out.m_Type = Json::Type::Array;
			SkipSpace();
			if (Consume("]"))
				return true;
			while (true)
			{
				Json item;
				if (!ParseValue(item, depth + 1))
					return false;
				out.m_Items.push_back(std::move(item));
				SkipSpace();
				if (Consume("]"))
					return true;
				if (!Consume(","))
					return false;
			}
		}

		bool ParseObject(Json& out, int depth)
		{
			++m_Pos;
			out.m_Type = Json::Type::Object;
			SkipSpace();
			if (Consume("}"))
				return true;
			while (true)
			{
				SkipSpace();
				std::string key;
				if (m_Pos >= m_Text.size() || m_Text[m_Pos] != '"' || !ParseString(key))
					return false;
				SkipSpace();
				if (!Consume(":"))
					return false;
				Json value;
				if (!ParseValue(value, depth + 1))
					return false;
				out.m_Members[key] = std::move(value);
				SkipSpace();
				if (Consume("}"))
					return true;
				if (!Consume(","))
					return false;
			}
		}

		std::string_view m_Text;
		size_t m_Pos = 0;
	};

	Json Json::Parse(std::string_view text)
	{
		Json out;
		if (!JsonParser(text).ParseDocument(out))
			return {};
		return out;
	}

	const Json& Json::operator[](std::string_view key) const
	{
		const auto found = m_Members.find(key);
		return found != m_Members.end() ? found->second : NullJson();
	}

	bool Json::Has(std::string_view key) const
	{
		return m_Members.find(key) != m_Members.end();
	}

	const Json& Json::operator[](int32 index) const
	{
		return index >= 0 && index < Num() ? m_Items[static_cast<size_t>(index)] : NullJson();
	}
}
