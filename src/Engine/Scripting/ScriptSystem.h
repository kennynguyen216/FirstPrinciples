#pragma once

#include "Engine/World.h"

namespace FP
{
	// Runs every ScriptComponent in a world while it plays. Add it before the game's own systems.
	class ScriptSystem : public System
	{
	public:
		const char* GetName() const override { return "Scripts"; }
		void OnStart(World& world) override;
		void Update(World& world, float deltaTime) override;
		void OnStop(World& world) override;

		// Used by hot reload. Suspend saves fields and frees instances, Resume rebuilds them.
		void SuspendInstances();
		void ResumeInstances();

	private:
		// Creates instances for slots that have none. Returns how many were created.
		int32 CreateMissing(World& world, bool callStart);
		void DestroyAll(World& world, bool callStop);
		static void OnScriptComponentDestroyed(entt::registry& registry, entt::entity entity);

		World* m_World = nullptr;
	};
}
