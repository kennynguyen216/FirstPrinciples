#pragma once

#include "Core/CoreMinimal.h"

#include <cstdio>
#include <string>
#include <type_traits>

namespace FP
{
	// One HTML page shown by a UI component. Scripts get it with GetUI() and talk to the page:
	//
	//   GetUI()->Set("score", 120);          // <span data-bind="score"></span> shows 120
	//   GetUI()->Emit("gameOver", "{\"won\":true}");  // engine.on("gameOver", data => ...)
	//
	// The page talks back with engine.send("name", data), which calls OnUIEvent on the scripts
	// of the entity holding the UI. Every call runs in the engine, so nothing here keeps code
	// from a script module that may reload.
	class UIDocument
	{
	public:
		virtual ~UIDocument() = default;

		// Sets engine.state[key] to a value given as JSON, then updates every element with
		// data-bind="key" and calls engine.watch handlers. Set() below is the easy way in.
		virtual void SetJson(std::string_view key, std::string_view json) = 0;

		// Fires an event the page hears with engine.on(name, data => ...). json is the data.
		virtual void Emit(std::string_view name, std::string_view json = "null") = 0;

		// Runs JavaScript in the page. Returns what it evaluates to, as text.
		virtual String RunScript(std::string_view script) = 0;

		// Swaps in another page, relative to Content.
		virtual void LoadPage(std::string_view path) = 0;

		// True once the page has loaded and its scripts have run.
		virtual bool IsReady() const = 0;

		// Numbers, bools and text, turned into JSON.
		template <typename T>
		void Set(std::string_view key, const T& value)
		{
			if constexpr (std::is_same_v<T, bool>)
				SetJson(key, value ? "true" : "false");
			else if constexpr (std::is_arithmetic_v<T>)
			{
				char text[64];
				std::snprintf(text, sizeof(text), "%.9g", static_cast<double>(value));
				SetJson(key, text);
			}
			else
				SetJson(key, ToJsonString(std::string_view(value)));
		}

		// Text as a quoted JSON string.
		static std::string ToJsonString(std::string_view text)
		{
			std::string out = "\"";
			for (const char c : text)
			{
				if (c == '"' || c == '\\')
				{
					out += '\\';
					out += c;
				}
				else if (c == '\n')
					out += "\\n";
				else if (c == '\r')
					out += "\\r";
				else if (c == '\t')
					out += "\\t";
				else if (static_cast<unsigned char>(c) < 0x20)
				{
					char escaped[8];
					std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
					out += escaped;
				}
				else
					out += c;
			}
			return out + "\"";
		}
	};
}
