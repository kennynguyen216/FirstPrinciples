#pragma once

#include "Engine/Engine.h"
#include "Engine/Animation/Animator.h"
#include "Core/Misc/ModuleContext.h"

#include <type_traits>

// Markers the header tool reads to generate reflection. They compile to nothing.
//
//   // Spins the entity. Comments right above the class show in the editor.
//   FPCLASS(Category = "Movement")
//   class Spinner : public Script
//   {
//   public:
//       FPPROPERTY(Range = "0, 720", Tooltip = "Degrees per second")
//       float Speed = 90;
//
//       FPFUNCTION()
//       void Reverse() { Speed = -Speed; }
//   };
//   FP_SCRIPT(Spinner);
//
// FPPROPERTY options: Tooltip, Category, Range = "min, max", ReadOnly and Asset = "Filter (*.ext)".
// A comment on the line above, or at the end of the line, is used as the tooltip.
// FPFUNCTION marks a function with no arguments as a button in the editor while playing.
// Marked members must be public, or the class must contain FP_BODY() to let the reflection in.
#define FPCLASS(...)
#define FPPROPERTY(...)
#define FPFUNCTION(...)
#define FP_BODY() template <typename> friend struct ::FP::ScriptReflection;

namespace FP
{
	// Base for C++ gameplay scripts. Attach scripts to entities with a ScriptComponent.
	// Derive from Script, override the On functions you need, mark members to expose with
	// FPPROPERTY and register the class with FP_SCRIPT(ClassName). See Source/Scripts in a
	// project for examples.
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

		// This entity's animator, for building a state machine and setting its parameters.
		// Null when the entity has no Animator component.
		Animator* GetAnimator() const { return m_World->GetAnimator(m_Entity); }

		// Removes this script's entity at the end of the frame.
		void DestroySelf() const { m_World->DestroyEntity(m_Entity); }

	private:
		friend class ScriptRuntime;

		World* m_World = nullptr;
		Entity m_Entity = NullEntity;
	};

	// A function the editor can call on a running script, from FPFUNCTION.
	struct ScriptFunction
	{
		const char* Name = nullptr;
		const char* Tooltip = nullptr;
		void (*Call)(Script* script) = nullptr;
	};

	// Everything the engine needs to know about one script class.
	struct ScriptType
	{
		const char* Name = nullptr;
		const char* SourceFile = nullptr; // where FP_SCRIPT was written, for opening in the editor
		const char* Description = nullptr; // what the script does, from the comment above the class
		const char* Category = nullptr;    // groups scripts in the editor's menus
		const char* Callbacks = nullptr;   // the On functions it overrides, like "OnStart, OnUpdate"
		Array<FieldInfo> Fields;
		Array<ScriptFunction> Functions;
		Script* (*Create)() = nullptr;
		void (*Destroy)(Script*) = nullptr;
	};

	// Lists a script's fields and functions. Hints apply to the last added field.
	template <typename T>
	class ScriptTypeBuilder
	{
	public:
		explicit ScriptTypeBuilder(ScriptType& type) : m_Fields(type.Fields), m_Functions(type.Functions) {}

		template <typename M>
		ScriptTypeBuilder& Field(const char* name, M T::*member) { m_Fields.Field(name, member); return *this; }
		ScriptTypeBuilder& Tooltip(const char* text) { m_Fields.Tooltip(text); return *this; }
		ScriptTypeBuilder& Category(const char* name) { m_Fields.Category(name); return *this; }
		ScriptTypeBuilder& Range(float min, float max) { m_Fields.Range(min, max); return *this; }
		ScriptTypeBuilder& ReadOnly() { m_Fields.ReadOnly(); return *this; }
		ScriptTypeBuilder& AssetPath(const char* filter) { m_Fields.AssetPath(filter); return *this; }
		ScriptTypeBuilder& Choices(const Array<const char*>& choices, bool editable = false) { m_Fields.Choices(choices, editable); return *this; }

		// A member function with no arguments, shown as a button: fields.Function<&MyScript::Jump>("Jump")
		template <auto Method>
		ScriptTypeBuilder& Function(const char* name, const char* tooltip = nullptr)
		{
			m_Functions.Add({ name, tooltip, [](Script* script) { (static_cast<T*>(script)->*Method)(); } });
			return *this;
		}

	private:
		FieldListBuilder<T> m_Fields;
		Array<ScriptFunction>& m_Functions;
	};

	// What the header tool knows about a script class. It writes a specialization for every
	// class marked with FPCLASS, and FP_SCRIPT reads it. Classes it never saw get this one.
	template <typename T>
	struct ScriptReflection
	{
		static constexpr const char* Description = nullptr;
		static constexpr const char* Category = nullptr;
		static constexpr const char* Callbacks = nullptr;

		template <typename U = T>
		static void Describe(ScriptTypeBuilder<U>&) {}
	};

	namespace ScriptTypes
	{
		// Scripts compiled into the current module. Each loaded module has its own list.
		Array<ScriptType*>& GetModuleList();
	}

	// Bump when the script interface changes so stale modules are refused instead of crashing.
	constexpr uint32 ScriptInterfaceVersion = 4;

	// Fingerprint of everything a script module shares with the host by layout. A module built
	// against different engine headers or a different configuration gives a different value.
	inline uint32 ComputeScriptAbi()
	{
		const uint64 parts[] = {
			ScriptInterfaceVersion, FP_BUILD_DEBUG, sizeof(void*), sizeof(World), sizeof(String), sizeof(Array<int32>),
			sizeof(TransformComponent), sizeof(ScriptType), sizeof(FieldInfo), sizeof(ScriptFunction), sizeof(ModuleContext),
			sizeof(AudioWorld), sizeof(AudioResult), sizeof(AudioPlayResult), sizeof(VoiceHandle), sizeof(OneShotParams),
			sizeof(AudioSourceComponent), sizeof(AudioListenerComponent),
			sizeof(AnimState), sizeof(AnimTransition), sizeof(AnimCondition), sizeof(AnimatorComponent), sizeof(SkeletalMeshComponent),
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
		// describe adds fields by hand after the generated ones. It may be null.
		ScriptRegistrar(const char* name, const char* sourceFile, void (*describe)(ScriptTypeBuilder<T>&))
		{
			m_Type.Name = name;
			m_Type.SourceFile = sourceFile;
			m_Type.Description = ScriptReflection<T>::Description;
			m_Type.Category = ScriptReflection<T>::Category;
			m_Type.Callbacks = ScriptReflection<T>::Callbacks;
			m_Type.Create = []() -> Script* { return new T(); };
			m_Type.Destroy = [](Script* script) { delete script; };
			ScriptTypeBuilder<T> builder(m_Type);
			ScriptReflection<T>::Describe(builder);
			if (describe)
				describe(builder);
			ScriptTypes::GetModuleList().Add(&m_Type);
		}

		~ScriptRegistrar() { ScriptTypes::GetModuleList().Remove(&m_Type); }

	private:
		ScriptType m_Type;
	};
}

// Registers a script class after its definition, with the fields and functions the header tool
// found: FP_SCRIPT(Spinner);
#define FP_SCRIPT(Type) static ::FP::ScriptRegistrar<Type> FPScriptRegistrar_##Type(#Type, __FILE__, nullptr)

// Registers a script class and lists fields by hand in the block that follows, for code the
// header tool does not see:
//   FP_SCRIPT_FIELDS(Spinner) { fields.Field("Speed", &Spinner::Speed).Range(0, 720); }
#define FP_SCRIPT_FIELDS(Type)                                                                              \
	static void FPScriptFields_##Type(::FP::ScriptTypeBuilder<Type>& fields);                              \
	static ::FP::ScriptRegistrar<Type> FPScriptRegistrar_##Type(#Type, __FILE__, &FPScriptFields_##Type); \
	static void FPScriptFields_##Type([[maybe_unused]] ::FP::ScriptTypeBuilder<Type>& fields)
