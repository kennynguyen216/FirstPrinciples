#pragma once

#include "Engine/Audio/AudioTypes.h"

namespace FP::Editor
{
	// Both paths are absolute UTF-8 paths. A successful result publishes a Content-relative path.
	AudioResult ImportAudioFile(const String& source, const String& destination, String& relativePath,
		const String& contentRoot = {});
}
