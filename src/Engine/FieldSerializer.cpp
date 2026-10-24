#include "Engine/FieldSerializer.h"

#include <cstdlib>

namespace FP::FieldSerializer
{
	namespace
	{
		void WriteQuoted(String& out, const String& text)
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

		float ParseFloat(const Array<String>& tokens, int32 index, float fallback = 0.0f)
		{
			return index < tokens.Num() ? std::strtof(tokens[index].CStr(), nullptr) : fallback;
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
			case FieldType::String: WriteQuoted(out, Get<String>(field, data)); break;
		}
		out += '\n';
	}

	void Read(const FieldInfo& field, void* data, const Array<String>& tokens)
	{
		switch (field.Type)
		{
			case FieldType::Bool: field.Get<bool>(data) = tokens.Num() > 1 && tokens[1] == "true"; break;
			case FieldType::Int: field.Get<int32>(data) = tokens.Num() > 1 ? std::atoi(tokens[1].CStr()) : 0; break;
			case FieldType::Float: field.Get<float>(data) = ParseFloat(tokens, 1); break;
			case FieldType::Vec2: field.Get<Vec2>(data) = { ParseFloat(tokens, 1), ParseFloat(tokens, 2) }; break;
			case FieldType::Vec3: field.Get<Vec3>(data) = { ParseFloat(tokens, 1), ParseFloat(tokens, 2), ParseFloat(tokens, 3) }; break;
			case FieldType::Color:
				field.Get<Color>(data) = { ParseFloat(tokens, 1), ParseFloat(tokens, 2), ParseFloat(tokens, 3), ParseFloat(tokens, 4, 1.0f) };
				break;
			case FieldType::String: field.Get<String>(data) = tokens.Num() > 1 ? tokens[1] : String(); break;
		}
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

	Array<String> Tokenize(std::string_view line)
	{
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
