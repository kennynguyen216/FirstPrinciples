#pragma once

#include "Core/String/String.h"

namespace FP::Paths
{
	// Repo root, found by walking up from the executable until a Content folder shows up.
	// Lets the same binary run from Binaries/, an IDE or a shipped folder.
	const String& GetProjectDir();

	const String& GetContentDir();

	// Absolute path for a path relative to Content.
	String Content(std::string_view relative);
}
