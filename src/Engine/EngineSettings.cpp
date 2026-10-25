#include "Engine/EngineSettings.h"
#include "Engine/SaveData.h"

#include <SDL3/SDL_filesystem.h>
#include <cmath>

namespace FP
{
	namespace
	{
		String SettingsPath()
		{
			char* directory = SDL_GetPrefPath("FirstPrinciples", "Engine");
			if (!directory) return {};
			String path = String(directory) + "Engine.settings";
			SDL_free(directory);
			return path;
		}

	}

	EngineSettings& EngineSettings::Get()
	{
		static EngineSettings settings = []() {
			EngineSettings value;
			const String path = SettingsPath();
			SDL_PathInfo info;
			if (!path.IsEmpty() && SDL_GetPathInfo(path.CStr(), &info) && !value.Load(path.CStr()))
				FP_LOG_WARN("Could not load engine settings %s; using defaults", path.CStr());
			return value;
		}();
		return settings;
	}

	const Array<const char*>& EngineSettings::ColorBlindModes()
	{
		// Indices match Present.frag.
		static const Array<const char*> modes{
			"None", "Protanopia", "Deuteranopia", "Tritanopia", "Protanomaly", "Deuteranomaly", "Tritanomaly", "Achromatopsia", "Achromatomaly"
		};
		return modes;
	}

	bool EngineSettings::IsValid() const
	{
		if (!std::isfinite(ColorBlindStrength) || ColorBlindStrength < 0 || ColorBlindStrength > 1) return false;
		for (const char* mode : ColorBlindModes())
			if (ColorBlindMode == mode) return true;
		return false;
	}

	bool EngineSettings::Load(const char* path)
	{
		SaveData data;
		if (!path || !data.LoadFromFile(path)) return false;
		EngineSettings candidate;
		candidate.ColorBlindMode = data.GetString("ColorBlindMode", "");
		candidate.ColorBlindStrength = data.GetFloat("ColorBlindStrength", -1);
		if (!candidate.IsValid()) return false;
		*this = candidate;
		return true;
	}

	bool EngineSettings::Save(const char* path) const
	{
		if (!IsValid()) return false;
		const String destination = path ? String(path) : SettingsPath();
		if (destination.IsEmpty()) return false;
		SaveData data;
		data.SetString("ColorBlindMode", ColorBlindMode.View());
		data.SetFloat("ColorBlindStrength", ColorBlindStrength);
		return data.SaveToFile(destination);
	}
}
