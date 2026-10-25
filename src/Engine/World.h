#pragma once

#include "CoreObject/CoreObject.h"
#include "Engine/Components.h"

#include <entt/entt.hpp>

#include <type_traits>
#include <memory>

namespace FP
{
	class World;
	class AudioWorld;
	class Animator;
	class Physics;
	class UIDocument;

	using Entity = entt::entity;
	inline constexpr entt::null_t NullEntity = entt::null;

	// Game logic. Systems run every frame while the world is playing, in the order they were added.
	class System : public RefCounted
	{
	public:
		virtual const char* GetName() const = 0;
		virtual void OnStart(World&) {}
		virtual void Update(World& world, float deltaTime) = 0;
		virtual void OnStop(World&) {}
	};

	// CPU time spent in a system's Update call during the latest game frame.
	struct SystemTiming
	{
		String Name;
		double LastMilliseconds = 0;
		double AverageMilliseconds = 0;
		uint64 Samples = 0;
	};

	// Holds the entities of one scene. Storage and queries are EnTT and this adds names, ids,
	// systems and play state on top.
	class World : public Object
	{
		FP_OBJECT(World, Object)

	public:
		World();
		~World() override;
		// Scripts dispatch into the host so audio state never retains code from a reloadable module.
		virtual AudioWorld& GetAudio();

		// The entity's animator, made on first use. Null without an AnimatorComponent.
		// Virtual for the same reason as GetAudio.
		virtual Animator* GetAnimator(Entity entity);

		// Bodies, forces and raycasts. Made on first use. Virtual for the same reason as GetAudio.
		virtual Physics& GetPhysics();
		// The page of the entity's UI component, made on first use. Null without one.
		// Virtual for the same reason as GetAudio.
		virtual UIDocument* GetUI(Entity entity);

		// ---- Entities ------------------------------------------------------

		// New entity with an id, a name and a transform.
		Entity CreateEntity(String name = "Entity", const Guid& id = Guid::New());

		// Entity with only an id. Used when loading so saved components fully decide the rest.
		Entity CreateEmptyEntity(const Guid& id = Guid::New());

		// Removes the entity and all of its children. Deferred until the end of Update while
		// playing so systems can destroy safely mid loop.
		void DestroyEntity(Entity entity);

		bool IsValid(Entity entity) const { return entity != NullEntity && m_Registry.valid(entity); }

		Entity FindEntityByName(std::string_view name) const;
		Entity FindEntityByGuid(const Guid& id) const;
		String GetEntityName(Entity entity) const;

		// Every entity in creation order.
		Array<Entity> GetEntities() const;
		uint64 GetCreationIndex(Entity entity) const;

		// The entity holding scene wide scripts, or NullEntity when the scene has none yet.
		Entity GetSceneEntity() const;
		Entity GetOrCreateSceneEntity();

		// ---- Hierarchy -----------------------------------------------------

		// NullEntity for top level entities.
		Entity GetParent(Entity entity) const;

		// Direct children in creation order.
		Array<Entity> GetChildren(Entity entity) const;

		// The first child with the name, searching grandchildren too when recursive.
		Entity FindChild(Entity parent, std::string_view name, bool recursive = false) const;

		// True when ancestor is the entity's parent, its parent's parent and so on.
		bool IsDescendantOf(Entity entity, Entity ancestor) const;

		// Makes child follow parent, or detaches it when parent is NullEntity. With
		// keepWorldTransform it stays where it is, otherwise its Transform is reused as is.
		// Returns false when the parent is the child itself or one of its descendants.
		bool SetParent(Entity child, Entity parent, bool keepWorldTransform = true);

		// Transforms after every parent is applied.
		Mat4 GetWorldMatrix(Entity entity) const;
		Vec3 GetWorldPosition(Entity entity) const;
		Quat GetWorldRotation(Entity entity) const;

		// The world transform as position, euler rotation and scale. Scale ignores any skew a
		// rotated parent with uneven scale would add.
		TransformComponent GetWorldTransform(Entity entity) const;

		// Moves the entity so its world position is position, whatever its parents are.
		void SetWorldPosition(Entity entity, Vec3 position);

		// Sets the local transform that puts the entity at a world position, rotation and scale.
		void SetWorldTransform(Entity entity, const TransformComponent& world);

		// Removes every entity.
		void Clear();
		void ReplaceScene(World& candidate);

		// ---- Cameras -------------------------------------------------------

		// The camera the game sees through: the first with Primary on. NullEntity when none is.
		Entity GetActiveCamera();

		// Sees through camera from now on by making it the only Primary camera. Returns false and
		// changes nothing when the entity has no camera.
		bool SetActiveCamera(Entity camera);

		// ---- Components ----------------------------------------------------

		template <typename T, typename... Args>
		decltype(auto) Add(Entity entity, Args&&... args)
		{
			return m_Registry.emplace_or_replace<T>(entity, std::forward<Args>(args)...);
		}

		template <typename T>
		T& Get(Entity entity) { return m_Registry.get<T>(entity); }

		template <typename T>
		T* TryGet(Entity entity) { return m_Registry.try_get<T>(entity); }

		template <typename... T>
		bool Has(Entity entity) const { return m_Registry.all_of<T...>(entity); }

		template <typename T>
		void Remove(Entity entity) { m_Registry.remove<T>(entity); }

		// Entities that have all of the listed components.
		// Usage: for (auto [entity, transform, sprite] : world.View<TransformComponent, SpriteComponent>().each())
		template <typename... T>
		auto View() { return m_Registry.view<T...>(); }

		entt::registry& GetRegistry() { return m_Registry; }

		// ---- Systems and play state ----------------------------------------

		template <typename T, typename... Args>
		T* AddSystem(Args&&... args)
		{
			RefPtr<T> system = MakeRef<T>(std::forward<Args>(args)...);
			m_Systems.Add(system);
			if (m_Playing)
				system->OnStart(*this);
			return system.Get();
		}

		void ClearSystems();
		const Array<RefPtr<System>>& GetSystems() const { return m_Systems; }

		void BeginPlay();
		void EndPlay();
		bool IsPlaying() const { return m_Playing; }
		void SetPaused(bool paused);
		bool IsPaused() const { return m_Paused; }
		// Advances one game update while paused, without resuming continuous play or audio.
		void Step(float deltaTime);
		const Array<SystemTiming>& GetSystemTimings() const { return m_SystemTimings; }
		double GetLastUpdateMilliseconds() const { return m_LastUpdateMilliseconds; }
		double GetAverageUpdateMilliseconds() const { return m_AverageUpdateMilliseconds; }
		uint64 GetProfileFrame() const { return m_ProfileFrame; }

		// Runs systems when playing, then flushes deferred destroys and any requested scene load.
		void Update(float deltaTime);

		// Swaps in a scene file, relative to Content, at the end of this frame's Update so
		// whatever asked can finish first. Play carries on in the new scene.
		void RequestSceneLoad(std::string_view path) { m_PendingScene = String(path); }

		double GetTime() const { return m_Time; }

		void OnDestroy() override;

	private:
		void RunFrame(float deltaTime);
		void FlushDestroyed();
		void SetLocalFromWorld(Entity entity, const Mat4& world);

		entt::registry m_Registry;
		Array<RefPtr<System>> m_Systems;
		Array<Entity> m_PendingDestroy;
		uint64 m_NextCreationIndex = 0;
		mutable Array<Entity> m_EntityOrder;
		mutable bool m_EntityOrderDirty = true;
		double m_Time = 0;
		bool m_Playing = false;
		bool m_Paused = false;
		Array<SystemTiming> m_SystemTimings;
		double m_LastUpdateMilliseconds = 0;
		double m_AverageUpdateMilliseconds = 0;
		uint64 m_ProfileFrame = 0;
		std::unique_ptr<AudioWorld> m_Audio;
		std::unique_ptr<Physics> m_Physics;
		String m_PendingScene;
	};
}
