#pragma once

#include "Engine/Scripting/Script.h"
#include "Engine/Scripting/ScriptComponent.h"
#include "Core/Misc/ModuleContext.h"

#include <functional>

struct SDL_SharedObject;

namespace FP
{
	class ScriptSystem;

	// Loads the game scripts library and creates script instances. Host side only.
	//
	// The library is copied to a fresh file before loading so the original can be rebuilt while
	// the copy is in use (Windows locks loaded DLLs). Reload swaps in a new build and moves every
	// running script across, keeping its exposed field values.
	class ScriptRuntime
	{
	public:
		static ScriptRuntime& Get();

		// GameScripts next to the running executable.
		static String GetDefaultModulePath();

		bool LoadModule(const String& path);
		void UnloadModule();
		bool IsModuleLoaded() const { return m_Module != nullptr; }
		const String& GetModulePath() const { return m_ModulePath; }

		// Unloads and loads the module again. Running scripts are rebuilt with their field values.
		bool Reload();

		// Types from the loaded module plus any compiled into the host itself.
		const ScriptType* FindType(std::string_view name) const;
		Array<const ScriptType*> GetTypes() const;

		Script* CreateInstance(const ScriptType& type, World* world, Entity entity, std::string_view properties) const;
		void DestroyInstance(const ScriptType& type, Script* instance) const;

		static void ApplyProperties(const ScriptType& type, Script* instance, std::string_view properties);
		static String CaptureProperties(const ScriptType& type, const Script* instance);

		// Told false right before the module unloads and true after one loads. Anything holding
		// pointers into script code (instances, field names) must drop them on false.
		using Listener = std::function<void(bool loaded)>;
		int32 AddListener(Listener listener);
		void RemoveListener(int32 id);

		void RegisterSystem(ScriptSystem* system);
		void UnregisterSystem(ScriptSystem* system);

	private:
		ScriptRuntime() = default;
		void Notify(bool loaded);

		SDL_SharedObject* m_Module = nullptr;
		const Array<ScriptType*>* m_ModuleTypes = nullptr;
		void (*m_Shutdown)() = nullptr;
		String m_ModulePath;
		String m_LoadedCopy;
		int32 m_LoadCount = 0;
		ModuleContext m_Context;

		struct ListenerEntry
		{
			int32 Id;
			Listener Fn;
		};
		Array<ListenerEntry> m_Listeners;
		int32 m_NextListenerId = 1;
		Array<ScriptSystem*> m_Systems;
	};
}
