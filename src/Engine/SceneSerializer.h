#pragma once

#include "Engine/World.h"

namespace FP
{
	// Saves and loads every registered component of every entity as readable text.
	// Unknown components and fields are skipped with a warning so old scenes keep loading.
	//
	//   entity 2f1c...e9
	//   component Transform
	//     Position 1 2
	//     Rotation 0
	namespace SceneSerializer
	{
		bool Validate(World& world);
		String SaveToString(World& world);

		// Replaces the world's entities with the scene. Returns false on a hard parse error.
		bool LoadFromString(World& world, std::string_view text);

		bool SaveToFile(World& world, const char* path);
		bool LoadFromFile(World& world, const char* path);
	}
}
