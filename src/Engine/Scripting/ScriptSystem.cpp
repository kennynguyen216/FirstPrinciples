#include "Engine/Scripting/ScriptSystem.h"
#include "Engine/Scripting/ScriptRuntime.h"

namespace FP
{
	void ScriptSystem::OnStart(World& world)
	{
		m_World = &world;
		ScriptRuntime::Get().RegisterSystem(this);
		world.GetRegistry().on_destroy<ScriptComponent>().connect<&ScriptSystem::OnScriptComponentDestroyed>();
		CreateMissing(world, true);
	}

	void ScriptSystem::Update(World& world, float deltaTime)
	{
		// Picks up entities spawned or scripts attached during play.
		CreateMissing(world, true);

		// Gather first so scripts can add components or spawn entities while updating.
		Array<Script*> instances;
		for (auto [entity, scripts] : world.View<ScriptComponent>().each())
		{
			for (ScriptSlot& slot : scripts.Scripts)
			{
				if (slot.Instance)
					instances.Add(slot.Instance);
			}
		}
		for (Script* script : instances)
			script->OnUpdate(deltaTime);
	}

	void ScriptSystem::OnStop(World& world)
	{
		DestroyAll(world, true);
		world.GetRegistry().on_destroy<ScriptComponent>().disconnect<&ScriptSystem::OnScriptComponentDestroyed>();
		ScriptRuntime::Get().UnregisterSystem(this);
		m_World = nullptr;
	}

	int32 ScriptSystem::CreateMissing(World& world, bool callStart)
	{
		ScriptRuntime& runtime = ScriptRuntime::Get();
		Array<Script*> created;
		for (auto [entity, scripts] : world.View<ScriptComponent>().each())
		{
			for (ScriptSlot& slot : scripts.Scripts)
			{
				if (slot.Instance || slot.Failed)
					continue;
				const ScriptType* type = runtime.FindType(slot.Name);
				if (!type)
				{
					FP_LOG_WARN("Script %s on %s is not compiled. Check the console for build errors.", slot.Name.CStr(), world.GetEntityName(entity).CStr());
					slot.Failed = true;
					continue;
				}
				slot.Type = type;
				slot.Instance = runtime.CreateInstance(*type, &world, entity, slot.Properties.View());
				created.Add(slot.Instance);
			}
		}
		// Start after creating them all so OnStart can find other scripts.
		for (Script* script : created)
		{
			if (callStart)
				script->OnStart();
			else
				script->OnHotReload();
		}
		return created.Num();
	}

	void ScriptSystem::DestroyAll(World& world, bool callStop)
	{
		ScriptRuntime& runtime = ScriptRuntime::Get();
		for (auto [entity, scripts] : world.View<ScriptComponent>().each())
		{
			for (ScriptSlot& slot : scripts.Scripts)
			{
				if (slot.Instance && callStop)
					slot.Instance->OnStop();
			}
		}
		for (auto [entity, scripts] : world.View<ScriptComponent>().each())
		{
			for (ScriptSlot& slot : scripts.Scripts)
			{
				if (slot.Instance)
					runtime.DestroyInstance(*slot.Type, slot.Instance);
				slot.Instance = nullptr;
				slot.Type = nullptr;
				slot.Failed = false;
			}
		}
	}

	void ScriptSystem::SuspendInstances()
	{
		if (!m_World)
			return;
		for (auto [entity, scripts] : m_World->View<ScriptComponent>().each())
		{
			for (ScriptSlot& slot : scripts.Scripts)
			{
				if (slot.Instance)
					slot.Properties = ScriptRuntime::CaptureProperties(*slot.Type, slot.Instance);
			}
		}
		DestroyAll(*m_World, false);
	}

	void ScriptSystem::ResumeInstances()
	{
		if (m_World)
			CreateMissing(*m_World, false);
	}

	void ScriptSystem::OnScriptComponentDestroyed(entt::registry& registry, entt::entity entity)
	{
		ScriptComponent& scripts = registry.get<ScriptComponent>(entity);
		for (ScriptSlot& slot : scripts.Scripts)
		{
			if (slot.Instance)
			{
				slot.Instance->OnStop();
				ScriptRuntime::Get().DestroyInstance(*slot.Type, slot.Instance);
				slot.Instance = nullptr;
			}
		}
	}
}
