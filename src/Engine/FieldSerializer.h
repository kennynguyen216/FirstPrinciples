#pragma once

#include "Engine/ComponentRegistry.h"

#include <type_traits>

// Reads and writes reflected fields as "Name value..." text lines.
// Shared by scene files and material files so both stay readable and diffable.
namespace FP::FieldSerializer
{
	// Appends "  Name value" plus a newline. indent is the leading text.
	void Write(String& out, const FieldInfo& field, const void* data, const char* indent = "  ");

	// tokens[0] is the field name and the rest are its values. Missing values become zero.
	void Read(const FieldInfo& field, void* data, const Array<String>& tokens);

	// Appends text in quotes with \" \\ and \n escaped so Tokenize reads it back unchanged.
	void AppendQuoted(String& out, const String& text);

	// Copies one field value from src to dst. Both point at the same struct type.
	void Copy(const FieldInfo& field, void* dst, const void* src);

	// Splits on whitespace and keeps quoted strings whole, handling \" \\ and \n escapes.
	Array<String> Tokenize(std::string_view line);

	// Calls fn(lineNumber, tokens) or fn(lineNumber, tokens, line) for every non empty, non comment line.
	// Returning false stops early.
	template <typename Fn>
	void ForEachLine(std::string_view text, Fn fn)
	{
		usize start = 0;
		int32 lineNumber = 0;
		while (start <= text.size())
		{
			usize end = text.find('\n', start);
			if (end == std::string_view::npos)
				end = text.size();
			const std::string_view line = text.substr(start, end - start);
			start = end + 1;
			++lineNumber;

			Array<String> tokens = Tokenize(line);
			if (tokens.IsEmpty() || tokens[0].StartsWith("#"))
				continue;
			bool keepGoing = true;
			if constexpr (std::is_invocable_v<Fn, int32, const Array<String>&, std::string_view>)
				keepGoing = fn(lineNumber, tokens, line);
			else
				keepGoing = fn(lineNumber, tokens);
			if (!keepGoing)
				return;
		}
	}
}
