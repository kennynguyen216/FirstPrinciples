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

		// One entity in the same text form, for sending it somewhere or copying it.
		String SaveEntityToString(World& world, Entity entity);

		// Adds the entity in text to the world without touching the others. Returns the new
		// entity, or NullEntity when the text is broken or an entity with its id already exists.
		Entity LoadEntityFromString(World& world, std::string_view text);

		// An entity followed by all of its descendants, for copying a whole branch.
		String SaveEntityTreeToString(World& world, Entity root);

		// Adds every entity in text as a copy with a new id. Parents inside the text are remapped
		// so the copies keep their shape, and a parent outside it that this world lacks is dropped.
		// Returns the new entities in text order, or nothing when the text is broken.
		Array<Entity> LoadEntitiesAsCopies(World& world, std::string_view text);

		// Adds every entity of a scene file as copies, like placing a prefab. Top level entities
		// are moved by offset. Returns them all in file order, or nothing when the file is broken.
		Array<Entity> SpawnFromFile(World& world, const char* path, Vec3 offset = {});

		bool SaveToFile(World& world, const char* path);
		bool LoadFromFile(World& world, const char* path);
	}
}
