#pragma once

#include "Engine/ComponentRegistry.h"

// Reads and writes reflected fields as "Name value..." text lines.
// Shared by scene files and material files so both stay readable and diffable.
namespace FP::FieldSerializer
{
	// Appends "  Name value" plus a newline. indent is the leading text.
	void Write(String& out, const FieldInfo& field, const void* data, const char* indent = "  ");

	bool Read(const FieldInfo& field, void* data, const Array<String>& tokens);

	// Copies one field value from src to dst. Both point at the same struct type.
	void Copy(const FieldInfo& field, void* dst, const void* src);

	// Splits on whitespace and keeps quoted strings whole, handling \" \\ and \n escapes.
	Array<String> Tokenize(std::string_view line, bool* valid = nullptr);

	// Calls fn(lineNumber, tokens) for every non empty, non comment line.
	template <typename Fn>
	bool ForEachLine(std::string_view text, Fn fn)
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
			const usize first = line.find_first_not_of(" \t\r");
			if (first == std::string_view::npos || line[first] == '#') continue;

			bool valid;
			Array<String> tokens = Tokenize(line, &valid);
			if (!valid)
				return false;
			if (tokens.IsEmpty() || tokens[0].StartsWith("#"))
				continue;
			if (!fn(lineNumber, tokens))
				return false;
		}
		return true;
	}
}
