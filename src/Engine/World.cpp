#include "Engine/World.h"
#include "Engine/ComponentRegistry.h"

namespace FP
{
	namespace
	{
		// Remembers creation order so the hierarchy and scene files stay stable.
		struct CreationOrderComponent
		{
			uint64 Index = 0;
		};
	}

	World::World()
	{
		// Make every storage here in engine code so none is first created inside a script module.
		(void)m_Registry.storage<CreationOrderComponent>();
		for (const ComponentInfo* info : ComponentRegistry::Get().GetAll())
			info->EnsureStorage(m_Registry);
	}

	Entity World::CreateEntity(String name, const Guid& id)
	{
		const Entity entity = CreateEmptyEntity(id);
		m_Registry.emplace<NameComponent>(entity, std::move(name));
		m_Registry.emplace<TransformComponent>(entity);
		return entity;
	}

	Entity World::CreateEmptyEntity(const Guid& id)
	{
		const Entity entity = m_Registry.create();
		m_Registry.emplace<IdComponent>(entity, id);
		m_Registry.emplace<CreationOrderComponent>(entity, m_NextCreationIndex++);
		return entity;
	}

	void World::DestroyEntity(Entity entity)
	{
		if (!IsValid(entity))
			return;
		if (m_Playing)
			m_PendingDestroy.AddUnique(entity);
		else
			m_Registry.destroy(entity);
	}

	Entity World::FindEntityByName(std::string_view name) const
	{
		for (auto [entity, nameComp] : m_Registry.view<NameComponent>().each())
		{
			if (nameComp.Name == name)
				return entity;
		}
		return NullEntity;
	}

	Entity World::FindEntityByGuid(const Guid& id) const
	{
		for (auto [entity, idComp] : m_Registry.view<IdComponent>().each())
		{
			if (idComp.Id == id)
				return entity;
		}
		return NullEntity;
	}

	String World::GetEntityName(Entity entity) const
	{
		const NameComponent* name = m_Registry.try_get<NameComponent>(entity);
		return name ? name->Name : String("Entity");
	}

	Array<Entity> World::GetEntities() const
	{
		struct Item
		{
			uint64 Index;
			Entity Value;
		};
		Array<Item> items;
		for (auto [entity, order] : m_Registry.view<CreationOrderComponent>().each())
			items.Add({ order.Index, entity });
		items.Sort([](const Item& a, const Item& b) { return a.Index < b.Index; });

		Array<Entity> result;
		result.Reserve(items.Num());
		for (const Item& item : items)
			result.Add(item.Value);
		return result;
	}

	void World::Clear()
	{
		m_Registry.clear();
		m_PendingDestroy.Clear();
		m_NextCreationIndex = 0;
	}

	void World::ClearSystems()
	{
		if (m_Playing)
		{
			for (RefPtr<System>& system : m_Systems)
				system->OnStop(*this);
		}
		m_Systems.Clear();
	}

	void World::BeginPlay()
	{
		if (m_Playing)
			return;
		m_Playing = true;
		m_Time = 0;
		for (int32 i = 0; i < m_Systems.Num(); ++i)
			m_Systems[i]->OnStart(*this);
		FlushDestroyed();
	}

	void World::EndPlay()
	{
		if (!m_Playing)
			return;
		for (RefPtr<System>& system : m_Systems)
			system->OnStop(*this);
		m_Playing = false;
		FlushDestroyed();
	}

	void World::Update(float deltaTime)
	{
		if (m_Playing)
		{
			m_Time += deltaTime;
			for (int32 i = 0; i < m_Systems.Num(); ++i)
				m_Systems[i]->Update(*this, deltaTime);
		}
		FlushDestroyed();
	}

	void World::FlushDestroyed()
	{
		for (Entity entity : m_PendingDestroy)
		{
			if (m_Registry.valid(entity))
				m_Registry.destroy(entity);
		}
		m_PendingDestroy.Clear();
	}

	void World::OnDestroy()
	{
		EndPlay();
		m_Systems.Clear();
		m_Registry.clear();
	}
}
