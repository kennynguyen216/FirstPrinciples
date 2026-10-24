#pragma once

#include "Core/CoreMinimal.h"

namespace FP
{
	class Script;
	struct ScriptType;

	// One script attached to an entity.
	struct ScriptSlot
	{
		String Name;

		// Saved field values as "Field value" lines. Kept as text so a scene still loads,
		// and keeps its values, when the script is missing or fails to compile.
		String Properties;

		// Only set while the world plays. Copies never share an instance.
		Script* Instance = nullptr;
		const ScriptType* Type = nullptr;
		bool Failed = false; // the script could not be created, so it is not retried every frame

		ScriptSlot() = default;
		ScriptSlot(String name, String properties = {}) : Name(std::move(name)), Properties(std::move(properties)) {}
		ScriptSlot(const ScriptSlot& other) : Name(other.Name), Properties(other.Properties) {}
		ScriptSlot& operator=(const ScriptSlot& other)
		{
			Name = other.Name;
			Properties = other.Properties;
			return *this;
		}

		// Moves happen when storage grows, so the running instance moves along.
		ScriptSlot(ScriptSlot&& other) noexcept
			: Name(std::move(other.Name)), Properties(std::move(other.Properties)), Instance(other.Instance), Type(other.Type), Failed(other.Failed)
		{
			other.Instance = nullptr;
			other.Type = nullptr;
		}

		ScriptSlot& operator=(ScriptSlot&& other) noexcept
		{
			Name = std::move(other.Name);
			Properties = std::move(other.Properties);
			Instance = other.Instance;
			Type = other.Type;
			Failed = other.Failed;
			other.Instance = nullptr;
			other.Type = nullptr;
			return *this;
		}
	};

	// Scripts attached to an entity. They run in order while the world plays.
	struct ScriptComponent
	{
		Array<ScriptSlot> Scripts;
	};

	namespace ScriptComponentFormat
	{
		// Scene text looks like:
		//   component Scripts
		//     script "Rotator"
		//       Speed 90
		void Write(String& out, const void* component);
		void Read(void* component, const Array<String>& tokens, std::string_view line);
	}
}
