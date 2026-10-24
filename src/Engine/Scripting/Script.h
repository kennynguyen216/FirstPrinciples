#pragma once

#include "Engine/Engine.h"
#include "Engine/Animation/Animator.h"
#include "Engine/Physics/Physics.h"
#include "Engine/SceneSerializer.h"
#include "Engine/Scripting/ScriptComponent.h"
#include "Core/Misc/ModuleContext.h"

#include <functional>
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

		// This entity's body started or stopped touching another. Needs a Collider on both.
		virtual void OnCollisionEnter(Entity other, const Collision& collision) { (void)other; (void)collision; }
		virtual void OnCollisionExit(Entity other) { (void)other; }

		// Something entered or left a trigger collider, this entity's or the other's.
		virtual void OnTriggerEnter(Entity other) { (void)other; }
		virtual void OnTriggerExit(Entity other) { (void)other; }

		World& GetWorld() const { return *m_World; }
		Entity GetEntity() const { return m_Entity; }
		// Relative to the parent when there is one. Use the World functions below for world space.
		TransformComponent& GetTransform() const { return m_World->Get<TransformComponent>(m_Entity); }

		// Seconds since play started.
		float GetTime() const { return static_cast<float>(m_World->GetTime()); }

		// ---- Components, on this entity or another ---------------------------

		template <typename T>
		T& Get() const { return m_World->Get<T>(m_Entity); }

		template <typename T>
		T* TryGet() const { return m_World->TryGet<T>(m_Entity); }

		template <typename T>
		T* TryGet(Entity entity) const { return m_World->IsValid(entity) ? m_World->TryGet<T>(entity) : nullptr; }

		template <typename T>
		bool Has() const { return m_World->Has<T>(m_Entity); }

		template <typename T, typename... Args>
		decltype(auto) Add(Args&&... args) const { return m_World->Add<T>(m_Entity, std::forward<Args>(args)...); }

		template <typename T>
		void Remove() const { m_World->Remove<T>(m_Entity); }

		// ---- Physics -------------------------------------------------------------

		// Forces, raycasts and gravity for the whole world.
		Physics& GetPhysics() const { return m_World->GetPhysics(); }

		// Pushes on this entity's rigid body. AddForce lasts one step, AddImpulse is a kick.
		void AddForce(Vec3 force) const { GetPhysics().AddForce(m_Entity, force); }
		void AddImpulse(Vec3 impulse) const { GetPhysics().AddImpulse(m_Entity, impulse); }
		Vec3 GetVelocity() const { return GetPhysics().GetVelocity(m_Entity); }
		void SetVelocity(Vec3 velocity) const { GetPhysics().SetVelocity(m_Entity, velocity); }

		// The first body along a ray, never this entity's own.
		bool Raycast(Vec3 origin, Vec3 direction, float maxDistance, RaycastHit& hit) const
		{
			return GetPhysics().Raycast(origin, direction, maxDistance, hit, m_Entity);
		}

		// This entity's animator, for building a state machine and setting its parameters.
		// Null when the entity has no Animator component.
		Animator* GetAnimator() const { return m_World->GetAnimator(m_Entity); }

		// ---- Finding entities and scripts ------------------------------------

		// The first entity with the name, or NullEntity.
		Entity FindEntity(std::string_view name) const { return m_World->FindEntityByName(name); }

		// Every entity with the name.
		Array<Entity> FindEntities(std::string_view name) const
		{
			Array<Entity> found;
			for (Entity entity : m_World->GetEntities())
			{
				if (m_World->GetEntityName(entity) == name)
					found.Add(entity);
			}
			return found;
		}

		bool IsValid(Entity entity) const { return m_World->IsValid(entity); }

		// The entity holding scene wide scripts. Scene scripts are on it themselves.
		Entity GetSceneEntity() const { return m_World->GetSceneEntity(); }
		String GetName(Entity entity) const { return m_World->GetEntityName(entity); }

		// A script of type T on an entity, this one by default. Null when it has none.
		template <typename T>
		T* GetScript(Entity entity = NullEntity) const
		{
			const ScriptComponent* scripts = TryGet<ScriptComponent>(entity == NullEntity ? m_Entity : entity);
			if (!scripts)
				return nullptr;
			for (const ScriptSlot& slot : scripts->Scripts)
			{
				if (T* script = dynamic_cast<T*>(slot.Instance))
					return script;
			}
			return nullptr;
		}

		// The first script of type T anywhere in the world, for managers and singletons.
		template <typename T>
		T* FindScript() const
		{
			for (auto [entity, scripts] : m_World->View<ScriptComponent>().each())
			{
				for (const ScriptSlot& slot : scripts.Scripts)
				{
					if (T* script = dynamic_cast<T*>(slot.Instance))
						return script;
				}
			}
			return nullptr;
		}

		// ---- Making and removing entities ------------------------------------

		// A new entity with a transform at position. Add components to fill it in.
		Entity Spawn(std::string_view name = "Entity", Vec3 position = {}) const
		{
			const Entity entity = m_World->CreateEntity(String(name));
			m_World->Get<TransformComponent>(entity).Position = position;
			return entity;
		}

		// Copies every entity in a scene file into this one, like placing a prefab. path is
		// relative to Content. Returns the new entities, top level ones first.
		Array<Entity> SpawnScene(std::string_view path, Vec3 position = {}) const
		{
			return SceneSerializer::SpawnFromFile(*m_World, Paths::Content(path).CStr(), position);
		}

		// Swaps the whole scene for another at the end of the frame. path is relative to Content.
		void LoadScene(std::string_view path) const { m_World->RequestSceneLoad(path); }

		// Removes an entity and its children at the end of the frame.
		void Destroy(Entity entity) const { m_World->DestroyEntity(entity); }
		void DestroySelf() const { m_World->DestroyEntity(m_Entity); }

		// ---- Parents and children, for this entity ----------------------------

		Entity GetParent() const { return m_World->GetParent(m_Entity); }
		Array<Entity> GetChildren() const { return m_World->GetChildren(m_Entity); }
		Entity FindChild(std::string_view name, bool recursive = false) const { return m_World->FindChild(m_Entity, name, recursive); }

		// Follows parent from now on, staying where it is. NullEntity detaches.
		bool SetParent(Entity parent, bool keepWorldTransform = true) const { return m_World->SetParent(m_Entity, parent, keepWorldTransform); }
		bool AddChild(Entity child, bool keepWorldTransform = true) const { return m_World->SetParent(child, m_Entity, keepWorldTransform); }

		Vec3 GetWorldPosition() const { return m_World->GetWorldPosition(m_Entity); }
		void SetWorldPosition(Vec3 position) const { m_World->SetWorldPosition(m_Entity, position); }
		Quat GetWorldRotation() const { return m_World->GetWorldRotation(m_Entity); }

		// ---- Timers ------------------------------------------------------------

		// Runs fn once after seconds of play. Returns an id for CancelTimer.
		int32 After(float seconds, std::function<void()> fn) { return AddTimer(seconds, 0, std::move(fn)); }

		// Runs fn every seconds of play until cancelled or the script stops.
		int32 Every(float seconds, std::function<void()> fn) { return AddTimer(seconds, seconds > 0 ? seconds : 0.0001f, std::move(fn)); }

		void CancelTimer(int32 id) { m_Timers.RemoveAll([id](const Timer& timer) { return timer.Id == id; }); }
		void CancelAllTimers() { m_Timers.Clear(); }

		// Called by the ScriptSystem before OnUpdate. Timers are lost on a hot reload.
		void TickTimers(float deltaTime)
		{
			// Fired timers can add or cancel others, so work on a copy of what is due.
			Array<std::function<void()>> due;
			for (Timer& timer : m_Timers)
			{
				timer.Remaining -= deltaTime;
				if (timer.Remaining > 0)
					continue;
				due.Add(timer.Fn);
				timer.Remaining = timer.Interval > 0 ? timer.Remaining + timer.Interval : 0;
				timer.Done = timer.Interval <= 0;
			}
			m_Timers.RemoveAll([](const Timer& timer) { return timer.Done; });
			for (std::function<void()>& fn : due)
				fn();
		}

	private:
		friend class ScriptRuntime;

		struct Timer
		{
			int32 Id = 0;
			float Remaining = 0;
			float Interval = 0; // 0 for one shot timers
			bool Done = false;
			std::function<void()> Fn;
		};

		int32 AddTimer(float delay, float interval, std::function<void()> fn)
		{
			const int32 id = ++m_NextTimer;
			m_Timers.Add({ id, delay, interval, false, std::move(fn) });
			return id;
		}

		World* m_World = nullptr;
		Entity m_Entity = NullEntity;
		Array<Timer> m_Timers;
		int32 m_NextTimer = 0;
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
	constexpr uint32 ScriptInterfaceVersion = 6;

	// Fingerprint of everything a script module shares with the host by layout. A module built
	// against different engine headers or a different configuration gives a different value.
	// Static so the module and the host each keep their own copy. A plain inline one is merged
	// across them when the module loads, and the module would then report the host's value.
	static inline uint32 ComputeScriptAbi()
	{
		const uint64 parts[] = {
			ScriptInterfaceVersion, FP_BUILD_DEBUG, sizeof(void*), sizeof(World), sizeof(String), sizeof(Array<int32>),
			sizeof(TransformComponent), sizeof(ScriptType), sizeof(FieldInfo), sizeof(ScriptFunction), sizeof(ModuleContext), sizeof(Script), sizeof(RaycastHit), sizeof(Collision),
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
