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

	// Holds the entities of one scene. Storage and queries are EnTT and this adds names, ids,
	// systems and play state on top.
	class World : public Object
	{
		FP_OBJECT(World, Object)

	public:
		World();
		~World() override;
		AudioWorld& GetAudio();
		// ---- Entities ------------------------------------------------------

		// New entity with an id, a name and a transform.
		Entity CreateEntity(String name = "Entity", const Guid& id = Guid::New());

		// Entity with only an id. Used when loading so saved components fully decide the rest.
		Entity CreateEmptyEntity(const Guid& id = Guid::New());

		// Deferred until the end of Update while playing so systems can destroy safely mid loop.
		void DestroyEntity(Entity entity);

		bool IsValid(Entity entity) const { return entity != NullEntity && m_Registry.valid(entity); }

		Entity FindEntityByName(std::string_view name) const;
		Entity FindEntityByGuid(const Guid& id) const;
		String GetEntityName(Entity entity) const;

		// Every entity in creation order.
		Array<Entity> GetEntities() const;

		// Removes every entity.
		void Clear();
		void ReplaceScene(World& candidate);

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

		// Runs systems when playing, then flushes deferred destroys.
		void Update(float deltaTime);

		double GetTime() const { return m_Time; }

		void OnDestroy() override;

	private:
		void FlushDestroyed();

		entt::registry m_Registry;
		Array<RefPtr<System>> m_Systems;
		Array<Entity> m_PendingDestroy;
		uint64 m_NextCreationIndex = 0;
		double m_Time = 0;
		bool m_Playing = false;
		std::unique_ptr<AudioWorld> m_Audio;
	};
}
