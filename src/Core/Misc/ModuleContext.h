#pragma once

#include "HAL/Platform.h"

namespace FP
{
	// Engine state shared with dynamically loaded modules such as the game scripts library.
	//
	// The engine is a static library, so a loaded module carries its own copy of every engine
	// global. The host hands its live state to the module through this struct, and each
	// subsystem's Get checks it first, so scripts see the same input, log, objects and registries
	// as the editor or game that loaded them. Pointers are opaque so Core does not depend on Engine.
	struct ModuleContext
	{
		void* Log = nullptr;
		void* Input = nullptr;
		void* GarbageCollector = nullptr;
		void* ComponentRegistry = nullptr;
		void* MaterialLibrary = nullptr;
		void* Paths = nullptr;
	};

	// Null in the host. Set once inside a loaded module before any of its code runs.
	const ModuleContext* GetModuleContext();
	void SetModuleContext(const ModuleContext* context);

	// Returns the host's instance when running inside a module, otherwise the local one.
	template <typename T>
	T& ResolveShared(void* ModuleContext::*member, T& local)
	{
		const ModuleContext* context = GetModuleContext();
		if (context && context->*member)
			return *static_cast<T*>(context->*member);
		return local;
	}
}
