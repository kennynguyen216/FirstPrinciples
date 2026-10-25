#pragma once

#include "Engine/Net/NetSession.h"
#include "Engine/Net/NetworkComponent.h"
#include "Engine/World.h"
#include "Engine/Audio/VoiceChat.h"

namespace FP
{
	// Keeps entities with a NetworkComponent in step across the peers of a session.
	//
	// Spawns travel as scene text on the reliable channel, split into chunks when long. Transforms
	// go out unreliably at each entity's send rate, stamped with a tick so an older update that
	// arrives late never wins. Remote entities glide toward the newest transform instead of
	// jumping between updates.
	//
	// Add it before the game's systems, start the session, then BeginPlay:
	//   NetworkSystem* network = world.AddSystem<NetworkSystem>();
	//   network->GetSession().Start(7777);
	class NetworkSystem : public System
	{
	public:
		const char* GetName() const override { return "Network"; }
		NetSession& GetSession() { return m_Session; }
		VoiceChat& GetVoiceChat() { return m_Voice; }

		void OnStart(World& world) override;
		void Update(World& world, float deltaTime) override;
		void OnStop(World& world) override;

		// The game's own messages, carried on the session alongside entity replication. A message
		// holds about 1000 bytes. to 0 sends to every peer.
		void SendGame(NetChannel channel, const void* data, int32 size, PeerId to = 0);
		// The next game message from another peer. Event.Data holds just what the sender passed.
		bool PollGame(NetEvent& out);

	private:
		enum class MessageType : uint8
		{
			Spawn,
			Despawn,
			Transforms,
			Voice = VoiceChat::MessageType,
			Game,
		};

		struct PartialSpawn
		{
			PeerId From = 0;
			Guid Id;
			String Text;
		};

		void ReplaceTemplates(World& world);
		void ClaimEntities(World& world);
		void HandleMessage(World& world, const NetEvent& event);
		void SendSpawn(World& world, Entity entity, PeerId to);
		void SendTransforms(World& world, double now);
		void Smooth(World& world, float deltaTime);

		NetSession m_Session;
		VoiceChat m_Voice;
		bool m_TemplatesReplaced = false;
		// Networked entities that came with the scene, and ones this peer spawned since.
		Array<Guid> m_SceneEntities;
		Array<Guid> m_Spawned;
		Array<PartialSpawn> m_Partial;
		Array<NetEvent> m_GameMessages;
		int32 m_GameHead = 0;
		uint32 m_Tick = 0;
	};
}
