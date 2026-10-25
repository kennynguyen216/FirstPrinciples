#pragma once

#include "Core/CoreMinimal.h"
#include <memory>

namespace FP
{
	class World;
	class NetSession;
	struct NetEvent;

	// One session's voice: microphone opt-in, V push-to-talk, and spatial remote speech.
	class VoiceChat
	{
	public:
		static constexpr uint8 MessageType = 3;
		VoiceChat();
		~VoiceChat();
		VoiceChat(const VoiceChat&) = delete;
		VoiceChat& operator=(const VoiceChat&) = delete;
		void SetEnabled(bool enabled);
		bool IsEnabled() const;
		void SetPeerMuted(uint64 peer, bool muted);
		bool IsPeerMuted(uint64 peer) const;
		String GetStatusText() const;
		void Update(World& world, NetSession& session, float deltaTime);
		void Receive(World& world, const NetEvent& event);
		void RemovePeer(uint64 peer);
		void Shutdown();

	private:
		friend bool VoiceChatOfflineSelfCheck();
		friend bool VoiceChatLoopbackSelfCheck();
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}
