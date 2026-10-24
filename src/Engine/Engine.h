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

namespace FP::Engine
{
	// Registers the built in components. Call once before loading scenes.
	void RegisterEngineComponents();
}
