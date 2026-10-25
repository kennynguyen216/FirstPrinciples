#include "Engine/World.h"
#include "Engine/Input.h"
#include "Engine/Animation/Animator.h"
#include "Engine/Physics/PhysicsWorld.h"
#include "Engine/UI/UISystem.h"
#include "Engine/Audio/AudioWorld.h"
#include "Engine/ComponentRegistry.h"
#include "Engine/Paths.h"
#include "Engine/SceneSerializer.h"

#include <chrono>

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

	World::~World() = default;

	AudioWorld& World::GetAudio()
	{
		if (!m_Audio) m_Audio = std::make_unique<AudioWorld>(*this);
		return *m_Audio;
	}

	Physics& World::GetPhysics()
	{
		if (!m_Physics) m_Physics = std::make_unique<PhysicsWorld>();
		return *m_Physics;
	}

	Animator* World::GetAnimator(Entity entity)
	{
		return AnimatorInstance::For(*this, entity);
	}

	UIDocument* World::GetUI(Entity entity)
	{
		return UISystem::For(*this, entity);
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
		m_EntityOrderDirty = true;
		const Entity entity = m_Registry.create();
		m_Registry.emplace<IdComponent>(entity, id);
		m_Registry.emplace<CreationOrderComponent>(entity, m_NextCreationIndex++);
		return entity;
	}

	void World::DestroyEntity(Entity entity)
	{
		if (!IsValid(entity))
			return;
		// Children first, so nothing is left pointing at a parent that is gone.
		for (Entity child : GetChildren(entity))
			DestroyEntity(child);
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

	Entity World::GetActiveCamera()
	{
		for (auto [entity, transform, camera] : View<TransformComponent, CameraComponent>().each())
		{
			if (camera.Primary)
				return entity;
		}
		return NullEntity;
	}

	bool World::SetActiveCamera(Entity camera)
	{
		if (!IsValid(camera) || !Has<TransformComponent, CameraComponent>(camera))
			return false;
		for (auto [entity, other] : View<CameraComponent>().each())
			other.Primary = entity == camera;
		return true;
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

	uint64 World::GetCreationIndex(Entity entity) const
	{
		return m_Registry.get<CreationOrderComponent>(entity).Index;
	}

	Array<Entity> World::GetEntities() const
	{
		const auto entities = m_Registry.view<CreationOrderComponent>();
		if (!m_EntityOrderDirty && m_EntityOrder.Num() == static_cast<int32>(entities.size()))
			return m_EntityOrder;
		struct Item
		{
			uint64 Index;
			Entity Value;
		};
		Array<Item> items;
		items.Reserve(static_cast<int32>(entities.size()));
		for (auto [entity, order] : entities.each())
			items.Add({ order.Index, entity });
		items.Sort([](const Item& a, const Item& b) { return a.Index < b.Index; });

		m_EntityOrder.Clear();
		m_EntityOrder.Reserve(items.Num());
		for (const Item& item : items)
			m_EntityOrder.Add(item.Value);
		m_EntityOrderDirty = false;
		return m_EntityOrder;
	}

	Entity World::GetSceneEntity() const
	{
		auto scenes = m_Registry.view<SceneComponent>();
		return scenes.begin() != scenes.end() ? *scenes.begin() : NullEntity;
	}

	Entity World::GetOrCreateSceneEntity()
	{
		Entity scene = GetSceneEntity();
		if (scene == NullEntity)
		{
			scene = CreateEntity("Scene");
			m_Registry.emplace<SceneComponent>(scene);
		}
		return scene;
	}

	Entity World::GetParent(Entity entity) const
	{
		const ParentComponent* parent = IsValid(entity) ? m_Registry.try_get<ParentComponent>(entity) : nullptr;
		if (!parent)
			return NullEntity;
		// The cached handle is checked against the id, since handles are reused after destroys.
		const IdComponent* cachedId = m_Registry.valid(parent->Cached) ? m_Registry.try_get<IdComponent>(parent->Cached) : nullptr;
		if (cachedId && cachedId->Id == parent->Parent)
			return parent->Cached;
		const Entity found = FindEntityByGuid(parent->Parent);
		const_cast<ParentComponent*>(parent)->Cached = found;
		return found;
	}

	Array<Entity> World::GetChildren(Entity entity) const
	{
		Array<Entity> children;
		if (!IsValid(entity))
			return children;
		const Guid id = m_Registry.get<IdComponent>(entity).Id;
		for (auto [candidate, parent, order] : m_Registry.view<ParentComponent, CreationOrderComponent>().each())
		{
			if (parent.Parent == id)
				children.Add(candidate);
		}
		children.Sort([&](Entity a, Entity b) {
			return m_Registry.get<CreationOrderComponent>(a).Index < m_Registry.get<CreationOrderComponent>(b).Index;
		});
		return children;
	}

	Entity World::FindChild(Entity parent, std::string_view name, bool recursive) const
	{
		for (Entity child : GetChildren(parent))
		{
			if (GetEntityName(child) == name)
				return child;
			if (recursive)
			{
				if (const Entity found = FindChild(child, name, true); found != NullEntity)
					return found;
			}
		}
		return NullEntity;
	}

	bool World::IsDescendantOf(Entity entity, Entity ancestor) const
	{
		// The depth limit stops a hand edited loop in a scene file from hanging.
		Entity current = GetParent(entity);
		for (int32 depth = 0; current != NullEntity && depth < 1000; ++depth)
		{
			if (current == ancestor)
				return true;
			current = GetParent(current);
		}
		return false;
	}

	bool World::SetParent(Entity child, Entity parent, bool keepWorldTransform)
	{
		if (!IsValid(child) || child == parent || (parent != NullEntity && (!IsValid(parent) || IsDescendantOf(parent, child))))
			return false;
		const Mat4 world = GetWorldMatrix(child);
		if (parent == NullEntity)
			m_Registry.remove<ParentComponent>(child);
		else
			m_Registry.emplace_or_replace<ParentComponent>(child, ParentComponent{ m_Registry.get<IdComponent>(parent).Id, parent });
		if (keepWorldTransform)
			SetLocalFromWorld(child, world);
		return true;
	}

	void World::SetLocalFromWorld(Entity entity, const Mat4& world)
	{
		TransformComponent* transform = m_Registry.try_get<TransformComponent>(entity);
		if (!transform)
			return;
		// The local transform that lands in the same place under the current parent.
		const Entity parent = GetParent(entity);
		const Mat4 local = parent == NullEntity ? world : GetWorldMatrix(parent).Inverse() * world;
		transform->Position = { local.At(3, 0), local.At(3, 1), local.At(3, 2) };
		Mat4 rotation = local;
		for (int c = 0; c < 3; ++c)
		{
			const float length = Vec3(local.At(c, 0), local.At(c, 1), local.At(c, 2)).Length();
			(&transform->Scale.X)[c] = length;
			for (int r = 0; r < 3; ++r)
				rotation.At(c, r) /= length > 1e-8f ? length : 1;
		}
		transform->Rotation = Quat::FromMatrix(rotation).ToEuler();
	}

	void World::SetWorldTransform(Entity entity, const TransformComponent& world)
	{
		if (!IsValid(entity))
			return;
		if (GetParent(entity) == NullEntity)
		{
			// Without a parent the values go straight in, so euler angles past 90 stay as typed.
			if (TransformComponent* transform = m_Registry.try_get<TransformComponent>(entity))
				*transform = world;
			return;
		}
		SetLocalFromWorld(entity, world.GetMatrix());
	}

	Mat4 World::GetWorldMatrix(Entity entity) const
	{
		const TransformComponent* transform = IsValid(entity) ? m_Registry.try_get<TransformComponent>(entity) : nullptr;
		const Mat4 local = transform ? transform->GetMatrix() : Mat4();
		Mat4 result = local;
		Entity parent = GetParent(entity);
		Entity fast = parent;
		while (parent != NullEntity)
		{
			if (const auto* ancestor = m_Registry.try_get<TransformComponent>(parent))
				result = ancestor->GetMatrix() * result;
			parent = GetParent(parent);
			fast = GetParent(GetParent(fast));
			// Two traversal speeds detect cycles without allocating a visited set
			if (parent != NullEntity && parent == fast) return local;
		}
		return result;
	}

	Vec3 World::GetWorldPosition(Entity entity) const
	{
		const Mat4 world = GetWorldMatrix(entity);
		return { world.At(3, 0), world.At(3, 1), world.At(3, 2) };
	}

	Quat World::GetWorldRotation(Entity entity) const
	{
		const TransformComponent* transform = IsValid(entity) ? m_Registry.try_get<TransformComponent>(entity) : nullptr;
		const Quat local = transform ? transform->GetRotation() : Quat();
		Quat result = local;
		Entity parent = GetParent(entity);
		Entity fast = parent;
		while (parent != NullEntity)
		{
			if (const auto* ancestor = m_Registry.try_get<TransformComponent>(parent))
				result = ancestor->GetRotation() * result;
			parent = GetParent(parent);
			fast = GetParent(GetParent(fast));
			if (parent != NullEntity && parent == fast) return local;
		}
		return result;
	}

	TransformComponent World::GetWorldTransform(Entity entity) const
	{
		TransformComponent out;
		const Mat4 world = GetWorldMatrix(entity);
		out.Position = { world.At(3, 0), world.At(3, 1), world.At(3, 2) };
		out.Rotation = GetWorldRotation(entity).ToEuler();
		for (int c = 0; c < 3; ++c)
			(&out.Scale.X)[c] = Vec3(world.At(c, 0), world.At(c, 1), world.At(c, 2)).Length();
		return out;
	}

	void World::SetWorldPosition(Entity entity, Vec3 position)
	{
		TransformComponent* transform = IsValid(entity) ? m_Registry.try_get<TransformComponent>(entity) : nullptr;
		if (!transform)
			return;
		const Entity parent = GetParent(entity);
		transform->Position = parent != NullEntity ? GetWorldMatrix(parent).Inverse().TransformPoint(position) : position;
	}

	void World::Clear()
	{
		m_Registry.clear();
		if (m_Audio) m_Audio->ResetScene();
		if (m_Physics) static_cast<PhysicsWorld&>(*m_Physics).Clear();
		m_PendingDestroy.Clear();
		m_NextCreationIndex = 0;
		m_EntityOrder.Clear();
		m_EntityOrderDirty = true;
	}

	void World::ReplaceScene(World& candidate)
	{
		if (&candidate == this) return;
		const bool wasPlaying = m_Playing;
		const bool wasPaused = m_Paused;
		if (wasPlaying) EndPlay();
		if (m_Audio) { m_Audio->ResetScene(); m_Audio->DisconnectRegistry(); }
		if (candidate.m_Audio) { candidate.m_Audio->ResetScene(); candidate.m_Audio->DisconnectRegistry(); }
		// Bodies belong to the entities they were made for, so neither world keeps any.
		if (m_Physics) static_cast<PhysicsWorld&>(*m_Physics).Clear();
		if (candidate.m_Physics) static_cast<PhysicsWorld&>(*candidate.m_Physics).Clear();
		m_Registry.swap(candidate.m_Registry);
		m_EntityOrderDirty = true;
		candidate.m_EntityOrderDirty = true;
		std::swap(m_NextCreationIndex, candidate.m_NextCreationIndex);
		m_PendingDestroy.Clear();
		if (m_Audio) m_Audio->ConnectRegistry();
		if (candidate.m_Audio) candidate.m_Audio->ConnectRegistry();
		if (wasPlaying)
		{
			BeginPlay();
			if (wasPaused) SetPaused(true);
		}
	}

	void World::ClearSystems()
	{
		if (m_Playing)
		{
			for (RefPtr<System>& system : m_Systems)
				system->OnStop(*this);
		}
		m_Systems.Clear();
		m_SystemTimings.Clear();
	}

	void World::BeginPlay()
	{
		if (m_Playing)
			return;
		m_Playing = true;
		m_Paused = false;
		m_Time = 0;
		m_SystemTimings.Clear();
		m_LastUpdateMilliseconds = 0;
		m_AverageUpdateMilliseconds = 0;
		m_ProfileFrame = 0;
		// Scripts that want a pointer ask for one in OnStart.
		Input::SetCursorMode(CursorMode::Locked);
		GetAudio().BeginPlay();
		for (int32 i = 0; i < m_Systems.Num(); ++i)
			m_Systems[i]->OnStart(*this);
		FlushDestroyed();
		GetAudio().Update();
	}

	void World::EndPlay()
	{
		if (!m_Playing)
			return;
		for (RefPtr<System>& system : m_Systems)
			system->OnStop(*this);
		if (m_Audio) m_Audio->EndPlay();
		m_Playing = false;
		m_Paused = false;
		FlushDestroyed();
	}

	void World::SetPaused(bool paused)
	{
		if (!m_Playing || m_Paused == paused)
			return;
		m_Paused = paused;
		if (m_Audio) m_Audio->SetPaused(paused);
	}

	void World::Update(float deltaTime)
	{
		if (m_Playing && m_Paused)
			return;
		RunFrame(deltaTime);
	}

	void World::Step(float deltaTime)
	{
		if (m_Playing && m_Paused)
			RunFrame(deltaTime);
	}

	void World::RunFrame(float deltaTime)
	{
		using Clock = std::chrono::steady_clock;
		const auto frameStart = Clock::now();
		if (m_Playing)
		{
			m_Time += deltaTime;
			for (int32 i = 0; i < m_Systems.Num(); ++i)
			{
				if (i >= m_SystemTimings.Num())
					m_SystemTimings.Add({ m_Systems[i]->GetName() });
				const auto start = Clock::now();
				m_Systems[i]->Update(*this, deltaTime);
				const double elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
				SystemTiming& timing = m_SystemTimings[i];
				timing.LastMilliseconds = elapsed;
				timing.AverageMilliseconds = timing.Samples ? timing.AverageMilliseconds * 0.9 + elapsed * 0.1 : elapsed;
				++timing.Samples;
			}
		}
		FlushDestroyed();
		if (!m_PendingScene.IsEmpty())
		{
			const String path = Paths::Content(m_PendingScene.View());
			m_PendingScene.Clear();
			if (SceneSerializer::LoadFromFile(*this, path.CStr()))
				FP_LOG("Loaded scene %s", path.CStr());
		}
		if (m_Playing && m_Audio) m_Audio->Update();
		if (m_Playing)
		{
			m_LastUpdateMilliseconds = std::chrono::duration<double, std::milli>(Clock::now() - frameStart).count();
			m_AverageUpdateMilliseconds = m_ProfileFrame ? m_AverageUpdateMilliseconds * 0.9 + m_LastUpdateMilliseconds * 0.1 : m_LastUpdateMilliseconds;
			++m_ProfileFrame;
		}
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
		if (m_Audio) m_Audio->EndPlay();
		m_Systems.Clear();
		m_Registry.clear();
	}
}
