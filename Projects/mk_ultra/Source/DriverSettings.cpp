#include "DriverSettings.h"

#include "Core/Log.h"
#include "Core/Misc/File.h"

#include <sstream>
#include <string>

bool LoadDriverSettings(const char* path, DriverSettings& out)
{
	FP::String text;
	if (!FP::File::ReadAllText(path, text))
	{
		FP_LOG_ERROR("Driver settings: could not read %s", path);
		return false;
	}

	struct Entry
	{
		const char* Name;
		float DriverSettings::*Member;
		bool Found;
	};
#define DRIVER_SETTING_ENTRY(name) { #name, &DriverSettings::name, false },
	Entry entries[] = { DRIVER_SETTINGS(DRIVER_SETTING_ENTRY) };
#undef DRIVER_SETTING_ENTRY

	out = {};
	std::istringstream lines(text.CStr());
	std::string line;
	while (std::getline(lines, line))
	{
		line = line.substr(0, line.find('#'));
		std::istringstream words(line);
		std::string name;
		float value = 0;
		if (!(words >> name))
			continue;
		if (!(words >> value))
		{
			FP_LOG_WARN("Driver settings: %s has no number", name.c_str());
			continue;
		}
		bool known = false;
		for (Entry& entry : entries)
		{
			if (name == entry.Name)
			{
				out.*entry.Member = value;
				entry.Found = known = true;
			}
		}
		if (!known)
			FP_LOG_WARN("Driver settings: unknown setting %s", name.c_str());
	}

	for (const Entry& entry : entries)
	{
		if (!entry.Found)
			FP_LOG_WARN("Driver settings: %s is missing, using 0", entry.Name);
	}
	return true;
}
