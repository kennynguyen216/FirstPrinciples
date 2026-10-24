// Entry points of the game scripts library. Compiled into GameScripts only, never into the engine.

#include "Engine/Scripting/Script.h"
#include "Core/Misc/ModuleContext.h"

#if FP_PLATFORM_WINDOWS
	#define FP_MODULE_EXPORT extern "C" __declspec(dllexport)
#else
	#define FP_MODULE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace
{
	// The host's context is copied so it stays valid for the module's whole life.
	FP::ModuleContext GHostContext;
}

FP_MODULE_EXPORT FP::uint32 FP_ScriptModuleAbi()
{
	return FP::ComputeScriptAbi();
}

FP_MODULE_EXPORT const FP::Array<FP::ScriptType*>* FP_ScriptModuleInit(const FP::ModuleContext* host)
{
	GHostContext = *host;
	FP::SetModuleContext(&GHostContext);
	return &FP::ScriptTypes::GetModuleList();
}

FP_MODULE_EXPORT void FP_ScriptModuleShutdown()
{
	FP::SetModuleContext(nullptr);
}
