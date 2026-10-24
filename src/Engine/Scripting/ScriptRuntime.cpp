#include "Engine/Scripting/ScriptRuntime.h"
#include "Engine/Scripting/ScriptSystem.h"
#include "Engine/FieldSerializer.h"
#include "Engine/Material.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_loadso.h>

#include <filesystem>

namespace FP
{
	namespace
	{
#if FP_PLATFORM_WINDOWS
		constexpr const char* ModuleFile = "GameScripts.dll";
#elif FP_PLATFORM_MACOS
		constexpr const char* ModuleFile = "GameScripts.dylib";
#else
		constexpr const char* ModuleFile = "GameScripts.so";
#endif

		using AbiFn = uint32 (*)();
		using InitFn = const Array<ScriptType*>* (*)(const ModuleContext*);
		using ShutdownFn = void (*)();
	}

	ScriptRuntime& ScriptRuntime::Get()
	{
		static ScriptRuntime runtime;
		return runtime;
	}

	String ScriptRuntime::GetDefaultModulePath()
	{
		const char* base = SDL_GetBasePath();
		return String(base ? base : "") + ModuleFile;
	}

	bool ScriptRuntime::LoadModule(const String& path)
	{
		namespace fs = std::filesystem;
		UnloadModule();
		m_ModulePath = path;
		m_AbiMismatch = false;

		std::error_code ec;
		const fs::path source(path.CStr());
		if (!fs::exists(source, ec))
		{
			FP_LOG_WARN("No script module at %s. Build the GameScripts project.", path.CStr());
			return false;
		}

		// Load a copy so the build can overwrite the original while this one is in use.
		const fs::path cacheDir = source.parent_path() / "ScriptCache";
		fs::create_directories(cacheDir, ec);
		if (m_LoadCount == 0)
		{
			// Leftovers from earlier runs. Files still locked by another editor are skipped.
			for (const fs::directory_entry& entry : fs::directory_iterator(cacheDir, ec))
				fs::remove(entry.path(), ec);
		}
		const fs::path copy = cacheDir / (source.stem().string() + "_" + std::to_string(++m_LoadCount) + source.extension().string());
		fs::copy_file(source, copy, fs::copy_options::overwrite_existing, ec);
		if (ec)
		{
			FP_LOG_ERROR("Could not copy script module: %s", ec.message().c_str());
			return false;
		}
		m_LoadedCopy = copy.string().c_str();

		m_Module = SDL_LoadObject(m_LoadedCopy.CStr());
		if (!m_Module)
		{
			FP_LOG_ERROR("Could not load script module: %s", SDL_GetError());
			return false;
		}

		auto abi = reinterpret_cast<AbiFn>(SDL_LoadFunction(m_Module, "FP_ScriptModuleAbi"));
		auto init = reinterpret_cast<InitFn>(SDL_LoadFunction(m_Module, "FP_ScriptModuleInit"));
		m_Shutdown = reinterpret_cast<ShutdownFn>(SDL_LoadFunction(m_Module, "FP_ScriptModuleShutdown"));
		if (!abi || !init || !m_Shutdown)
		{
			FP_LOG_ERROR("%s is not a script module", path.CStr());
			UnloadModule();
			return false;
		}
		if (abi() != ComputeScriptAbi())
		{
			FP_LOG_ERROR("Scripts and this program were built from different engine code, so the scripts were not loaded. "
				"Rebuild the scripts, or rebuild the program if it is the older one (Scripts > Rebuild Editor and Restart).");
			m_AbiMismatch = true;
			m_Shutdown = nullptr;
			UnloadModule();
			return false;
		}

		// Hand the module our live engine state before any script runs.
		m_Context.Log = Log::GetSharedState();
		m_Context.Input = Input::GetSharedState();
		m_Context.GarbageCollector = &GarbageCollector::Get();
		m_Context.ComponentRegistry = &ComponentRegistry::Get();
		m_Context.MaterialLibrary = &MaterialLibrary::Get();
		m_ModuleTypes = init(&m_Context);

		FP_LOG("Loaded %d scripts from %s", m_ModuleTypes ? m_ModuleTypes->Num() : 0, ModuleFile);
		Notify(true);
		return true;
	}

	void ScriptRuntime::UnloadModule()
	{
		if (!m_Module)
			return;
		Notify(false);
		if (m_Shutdown)
			m_Shutdown();
		SDL_UnloadObject(m_Module);
		m_Module = nullptr;
		m_ModuleTypes = nullptr;
		m_Shutdown = nullptr;

		std::error_code ec;
		std::filesystem::remove(m_LoadedCopy.CStr(), ec);
	}

	bool ScriptRuntime::Reload()
	{
		const String path = m_ModulePath.IsEmpty() ? GetDefaultModulePath() : m_ModulePath;

		// Running scripts save their fields and let go of the old code.
		for (ScriptSystem* system : m_Systems)
			system->SuspendInstances();

		const bool loaded = LoadModule(path);

		for (ScriptSystem* system : m_Systems)
			system->ResumeInstances();
		return loaded;
	}

	const ScriptType* ScriptRuntime::FindType(std::string_view name) const
	{
		for (const ScriptType* type : GetTypes())
		{
			if (name == type->Name)
				return type;
		}
		return nullptr;
	}

	Array<const ScriptType*> ScriptRuntime::GetTypes() const
	{
		Array<const ScriptType*> types;
		for (const ScriptType* type : ScriptTypes::GetModuleList())
			types.Add(type);
		if (m_ModuleTypes)
		{
			for (const ScriptType* type : *m_ModuleTypes)
				types.Add(type);
		}
		types.Sort([](const ScriptType* a, const ScriptType* b) { return std::string_view(a->Name) < std::string_view(b->Name); });
		return types;
	}

	Script* ScriptRuntime::CreateInstance(const ScriptType& type, World* world, Entity entity, std::string_view properties) const
	{
		Script* instance = type.Create();
		instance->m_World = world;
		instance->m_Entity = entity;
		ApplyProperties(type, instance, properties);
		return instance;
	}

	void ScriptRuntime::DestroyInstance(const ScriptType& type, Script* instance) const
	{
		if (instance)
			type.Destroy(instance);
	}

	void ScriptRuntime::ApplyProperties(const ScriptType& type, Script* instance, std::string_view properties)
	{
		FieldSerializer::ForEachLine(properties, [&](int32, const Array<String>& tokens) {
			for (const FieldInfo& field : type.Fields)
			{
				if (tokens[0] == field.Name)
				{
					FieldSerializer::Read(field, instance, tokens);
					break;
				}
			}
			// Values for fields the script no longer has are dropped quietly.
			return true;
		});
	}

	String ScriptRuntime::CaptureProperties(const ScriptType& type, const Script* instance)
	{
		String out;
		for (const FieldInfo& field : type.Fields)
			FieldSerializer::Write(out, field, instance, "");
		return out;
	}

	int32 ScriptRuntime::AddListener(Listener listener)
	{
		const int32 id = m_NextListenerId++;
		m_Listeners.Add({ id, std::move(listener) });
		return id;
	}

	void ScriptRuntime::RemoveListener(int32 id)
	{
		m_Listeners.RemoveAll([id](const ListenerEntry& entry) { return entry.Id == id; });
	}

	void ScriptRuntime::Notify(bool loaded)
	{
		for (const ListenerEntry& entry : m_Listeners)
			entry.Fn(loaded);
	}

	void ScriptRuntime::RegisterSystem(ScriptSystem* system)
	{
		m_Systems.AddUnique(system);
	}

	void ScriptRuntime::UnregisterSystem(ScriptSystem* system)
	{
		m_Systems.Remove(system);
	}
}
