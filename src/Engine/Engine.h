#pragma once

// One include for game code.

#include "Core/CoreMinimal.h"
#include "CoreObject/CoreObject.h"
#include "Engine/ComponentRegistry.h"
#include "Engine/Components.h"
#include "Engine/Input.h"
#include "Engine/Math.h"
#include "Engine/Paths.h"
#include "Engine/World.h"
#include "Engine/Audio/AudioWorld.h"

namespace FP::Engine
{
	// File picker filter for texture fields.
	extern const char* const ImageFilter;

	// File picker filter for mesh fields.
	extern const char* const MeshFilter;
	extern const char* const SkeletalMeshFilter;

	// Registers the built in components. Call once before loading scenes.
	void RegisterEngineComponents();
}
