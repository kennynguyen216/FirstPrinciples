#pragma once

#include "Engine/Engine.h"
#include "Core/Misc/ModuleContext.h"

#include <type_traits>

namespace FP
{
	// Base for C++ gameplay scripts. Attach scripts to entities with a ScriptComponent.
	// Derive from Script, override the On functions you need, then register the class with
	// FP_SCRIPT(ClassName) followed by a block that calls fields.Field for each member to expose.
	// See Game/Scripts for examples.
	//
	// Exposed fields show in the inspector, save with the scene and survive hot reloads.
	// Anything else is reset when scripts reload.
	class Script
	{
	public:
		virtual ~Script() = default;

		// Called once when play starts, after every script in the world exists.
		virtual void OnStart() {}
		virtual void OnUpdate(float deltaTime) { (void)deltaTime; }
		virtual void OnStop() {}

		// Called instead of OnStart after a hot reload while playing. Fields already hold their old values.
		virtual void OnHotReload() { OnStart(); }

		World& GetWorld() const { return *m_World; }
		Entity GetEntity() const { return m_Entity; }
		TransformComponent& GetTransform() const { return m_World->Get<TransformComponent>(m_Entity); }

		template <typename T>
		T& Get() const { return m_World->Get<T>(m_Entity); }

		template <typename T>
		T* TryGet() const { return m_World->TryGet<T>(m_Entity); }

		template <typename T>
		bool Has() const { return m_World->Has<T>(m_Entity); }

		template <typename T, typename... Args>
		decltype(auto) Add(Args&&... args) const { return m_World->Add<T>(m_Entity, std::forward<Args>(args)...); }

		Entity FindEntity(std::string_view name) const { return m_World->FindEntityByName(name); }

		// Removes this script's entity at the end of the frame.
		void DestroySelf() const { m_World->DestroyEntity(m_Entity); }

	private:
		friend class ScriptRuntime;

		World* m_World = nullptr;
		Entity m_Entity = NullEntity;
	};

	// Everything the engine needs to know about one script class.
	struct ScriptType
	{
		const char* Name = nullptr;
		const char* SourceFile = nullptr; // where FP_SCRIPT was written, for opening in the editor
		Array<FieldInfo> Fields;
		Script* (*Create)() = nullptr;
		void (*Destroy)(Script*) = nullptr;
	};

	namespace ScriptTypes
	{
		// Scripts compiled into the current module. Each loaded module has its own list.
		Array<ScriptType*>& GetModuleList();
	}

	// Bump when the script interface changes so stale modules are refused instead of crashing.
	constexpr uint32 ScriptInterfaceVersion = 2;

	// Fingerprint of everything a script module shares with the host by layout. A module built
	// against different engine headers or a different configuration gives a different value.
	inline uint32 ComputeScriptAbi()
	{
		const uint64 parts[] = {
			ScriptInterfaceVersion, FP_BUILD_DEBUG, sizeof(void*), sizeof(World), sizeof(String), sizeof(Array<int32>),
			sizeof(TransformComponent), sizeof(ScriptType), sizeof(FieldInfo), sizeof(ModuleContext),
			sizeof(AudioWorld), sizeof(AudioResult), sizeof(AudioPlayResult), sizeof(VoiceHandle), sizeof(OneShotParams),
			sizeof(AudioSourceComponent), sizeof(AudioListenerComponent),
		};
		return HashBytes(parts, sizeof(parts));
	}

	// Registers a script class while its module loads. Lives as a static so its type data is
	// freed with the module.
	template <typename T>
	class ScriptRegistrar
	{
		static_assert(std::is_base_of_v<Script, T>, "Scripts must derive from FP::Script");

	public:
		ScriptRegistrar(const char* name, const char* sourceFile, void (*describe)(FieldListBuilder<T>&))
		{
			m_Type.Name = name;
			m_Type.SourceFile = sourceFile;
			m_Type.Create = []() -> Script* { return new T(); };
			m_Type.Destroy = [](Script* script) { delete script; };
			FieldListBuilder<T> fields(m_Type.Fields);
			describe(fields);
			ScriptTypes::GetModuleList().Add(&m_Type);
		}

		~ScriptRegistrar() { ScriptTypes::GetModuleList().Remove(&m_Type); }

	private:
		ScriptType m_Type;
	};
}

// Registers a script class. Follow it with a block that lists the exposed fields.
#define FP_SCRIPT(Type)                                                                                     \
	static void FPScriptFields_##Type(::FP::FieldListBuilder<Type>& fields);                               \
	static ::FP::ScriptRegistrar<Type> FPScriptRegistrar_##Type(#Type, __FILE__, &FPScriptFields_##Type); \
	static void FPScriptFields_##Type([[maybe_unused]] ::FP::FieldListBuilder<Type>& fields)
