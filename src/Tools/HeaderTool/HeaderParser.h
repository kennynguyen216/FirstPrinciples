#pragma once

// Reads FPCLASS, FPPROPERTY and FPFUNCTION markers out of script source and writes the
// reflection header FP_SCRIPT reads. Plain standard C++ so the tool builds in seconds and
// never waits on the engine.

#include <string>
#include <vector>

namespace FP::HeaderTool
{
	struct Property
	{
		std::string Name;
		std::string Tooltip;
		std::string Category;
		std::string Asset; // file picker filter, like "Scenes (*.scene)"
		bool HasRange = false;
		float Min = 0;
		float Max = 0;
		bool ReadOnly = false;
		int Line = 0;
	};

	struct Function
	{
		std::string Name;
		std::string Tooltip;
		int Line = 0;
	};

	struct Class
	{
		std::string Name;
		std::string Namespace; // like "Game::Enemies", empty at global scope
		bool IsStruct = false;
		std::string Description;
		std::string Category;
		std::vector<std::string> Callbacks; // OnStart, OnUpdate and so on that it defines
		std::vector<Property> Properties;
		std::vector<Function> Functions;
		std::string File;
		int Line = 0;
	};

	struct Error
	{
		std::string File;
		int Line = 0;
		std::string Message;
	};

	// Finds every class marked with FPCLASS or deriving from Script in one file.
	void ParseFile(const std::string& text, const std::string& file, std::vector<Class>& classes, std::vector<Error>& errors);

	// The header text for all classes. Same input, same text, so unchanged builds stay unchanged.
	std::string Generate(const std::vector<Class>& classes, const std::string& source);
}
