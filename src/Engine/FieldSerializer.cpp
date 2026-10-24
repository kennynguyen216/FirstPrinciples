#include "Engine/FieldSerializer.h"

#include <cstdlib>
#include <cmath>
#include <limits>

namespace FP::FieldSerializer
{
	namespace
	{
		void WriteQuotedImpl(String& out, const String& text)
		{
			out += '"';
			for (char c : text)
			{
				if (c == '\n')
				{
					out += "\\n";
					continue;
				}
				if (c == '"' || c == '\\')
					out += '\\';
				out += c;
			}
			out += '"';
		}

		template <typename T>
		const T& Get(const FieldInfo& field, const void* data)
		{
			return *reinterpret_cast<const T*>(static_cast<const uint8*>(data) + field.Offset);
		}
	}

	void Write(String& out, const FieldInfo& field, const void* data, const char* indent)
	{
		out.Appendf("%s%s ", indent, field.Name);
		switch (field.Type)
		{
			case FieldType::Bool: out += Get<bool>(field, data) ? "true" : "false"; break;
			case FieldType::Int: out.Appendf("%d", Get<int32>(field, data)); break;
			case FieldType::Float: out.Appendf("%.9g", Get<float>(field, data)); break;
			case FieldType::Vec2:
			{
				const Vec2& v = Get<Vec2>(field, data);
				out.Appendf("%.9g %.9g", v.X, v.Y);
				break;
			}
			case FieldType::Vec3:
			{
				const Vec3& v = Get<Vec3>(field, data);
				out.Appendf("%.9g %.9g %.9g", v.X, v.Y, v.Z);
				break;
			}
			case FieldType::Color:
			{
				const Color& c = Get<Color>(field, data);
				out.Appendf("%.9g %.9g %.9g %.9g", c.R, c.G, c.B, c.A);
				break;
			}
			case FieldType::String: WriteQuotedImpl(out, Get<String>(field, data)); break;
		}
		out += '\n';
	}

	bool Read(const FieldInfo& field, void* data, const Array<String>& tokens)
	{
		const int32 count = field.Type == FieldType::Vec2 ? 2 : field.Type == FieldType::Vec3 ? 3 : field.Type == FieldType::Color ? 4 : 1;
		if (tokens.Num() != count + 1 && !(field.Type == FieldType::Color && tokens.Num() == 4))
			return false;
		float values[4] = { 0, 0, 0, 1 };
		if (field.Type != FieldType::Bool && field.Type != FieldType::Int && field.Type != FieldType::String)
		{
			for (int32 i = 1; i < tokens.Num(); ++i)
			{
				double value;
				if (!tokens[i].ToFloat(value) || !std::isfinite(value) || std::fabs(value) > std::numeric_limits<float>::max())
					return false;
				values[i - 1] = static_cast<float>(value);
			}
		}
		switch (field.Type)
		{
			case FieldType::Bool:
				if (tokens[1] != "true" && tokens[1] != "false") return false;
				field.Get<bool>(data) = tokens[1] == "true";
				break;
			case FieldType::Int:
			{
				int64 value;
				if (!tokens[1].ToInt(value) || value < std::numeric_limits<int32>::min() || value > std::numeric_limits<int32>::max()) return false;
				field.Get<int32>(data) = static_cast<int32>(value);
				break;
			}
			case FieldType::Float: field.Get<float>(data) = values[0]; break;
			case FieldType::Vec2: field.Get<Vec2>(data) = { values[0], values[1] }; break;
			case FieldType::Vec3: field.Get<Vec3>(data) = { values[0], values[1], values[2] }; break;
			case FieldType::Color:
				field.Get<Color>(data) = { values[0], values[1], values[2], values[3] };
				break;
			case FieldType::String: field.Get<String>(data) = tokens[1]; break;
		}
		return true;
	}

	void AppendQuoted(String& out, const String& text)
	{
		WriteQuotedImpl(out, text);
	}

	void Copy(const FieldInfo& field, void* dst, const void* src)
	{
		switch (field.Type)
		{
			case FieldType::Bool: field.Get<bool>(dst) = Get<bool>(field, src); break;
			case FieldType::Int: field.Get<int32>(dst) = Get<int32>(field, src); break;
			case FieldType::Float: field.Get<float>(dst) = Get<float>(field, src); break;
			case FieldType::Vec2: field.Get<Vec2>(dst) = Get<Vec2>(field, src); break;
			case FieldType::Vec3: field.Get<Vec3>(dst) = Get<Vec3>(field, src); break;
			case FieldType::Color: field.Get<Color>(dst) = Get<Color>(field, src); break;
			case FieldType::String: field.Get<String>(dst) = Get<String>(field, src); break;
		}
	}

	Array<String> Tokenize(std::string_view line, bool* valid)
	{
		if (valid) *valid = true;
		Array<String> tokens;
		usize i = 0;
		auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
		while (i < line.size())
		{
			while (i < line.size() && isSpace(line[i]))
				++i;
			if (i >= line.size())
				break;
			String token;
			if (line[i] == '"')
			{
				++i;
				while (i < line.size() && line[i] != '"')
				{
					if (line[i] == '\\' && i + 1 < line.size())
					{
						++i;
						token += line[i] == 'n' ? '\n' : line[i];
					}
					else
					{
						token += line[i];
					}
					++i;
				}
				if (i >= line.size() || (i + 1 < line.size() && !isSpace(line[i + 1])))
				{
					if (valid) *valid = false;
					return {};
				}
				++i;
			}
			else
			{
				while (i < line.size() && !isSpace(line[i]))
					token += line[i++];
			}
			tokens.Add(std::move(token));
		}
		return tokens;
	}
}
