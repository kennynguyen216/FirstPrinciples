#pragma once

#include "Core/CoreMinimal.h"

namespace FP
{
	// Per-user engine preferences, shared by the editor and runtime across scenes and projects.
	struct EngineSettings
	{
		String ColorBlindMode = "None";
		float ColorBlindStrength = 1;

		static EngineSettings& Get();
		static const Array<const char*>& ColorBlindModes();
		bool IsValid() const;
		bool Load(const char* path);
		bool Save(const char* path = nullptr) const;
	};
}
