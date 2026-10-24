#pragma once

#include "Core/String/String.h"

namespace FP::File
{
	bool ReadAllText(const char* path, String& out);
	bool WriteAllText(const char* path, std::string_view text);
	bool Exists(const char* path);
}
