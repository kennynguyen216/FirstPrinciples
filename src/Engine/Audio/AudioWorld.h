#pragma once

#include "Engine/Audio/AudioTypes.h"
#include "Engine/World.h"

#include <vector>
#include <thread>

namespace FP
{
	class AudioDevice;

	struct AudioSpatialGains { float Left = 0; float Right = 0; };
	AudioSpatialGains CalculateAudioSpatialGains(Vec3 source, const TransformComponent& listener,
		float minDistance, float maxDistance);

	// Main-thread scene audio. Components contain authored data only; voices live here.
	class AudioWorld
	{
	public:
		explicit AudioWorld(World& world);
		virtual ~AudioWorld();
		AudioWorld(const AudioWorld&) = delete;
		AudioWorld& operator=(const AudioWorld&) = delete;

		virtual bool Attach(AudioDevice* device);
		virtual AudioDevice* GetDevice() const;
		virtual void BeginPlay();
		virtual void EndPlay();
		virtual void Update();
		virtual AudioResult Preload(const String& clip);
		virtual AudioPlayResult Play(Entity source);
		virtual AudioPlayResult PlayOneShot(const String& clip, const OneShotParams& params = {});
		virtual bool Stop(VoiceHandle voice);
		virtual bool Pause(VoiceHandle voice);
		virtual bool Resume(VoiceHandle voice);
		virtual VoiceState GetState(VoiceHandle voice) const;
		virtual VoiceHandle GetSourceVoice(Entity source) const;
		virtual void StopSource(Entity source);
		virtual void StopAll();
		virtual Entity GetListenerEntity() const { return m_ListenerEntity; }

	private:
		friend class World;
		struct Source
		{
			Entity Owner = NullEntity;
			VoiceHandle Voice;
			String RequestedClip;
		};
		struct Voice
		{
			VoiceHandle Handle;
			Entity Owner = NullEntity;
			bool Primary = false;
			bool Spatial = false;
			Vec3 Position;
			float MinDistance = 1;
			float MaxDistance = 25;
			AudioPlaybackParams Params;
			int Channels = 1;
			bool SettingsValid = true;
		};
		AudioPlayResult PlayInternal(Entity source, bool preservePause);
		Source& TrackSource(Entity source);
		void ResetScene();
		void ConnectRegistry();
		void DisconnectRegistry();
		void SourceRemoved(entt::registry&, Entity entity);
		void SelectListener();
		void ApplySpatial(Voice& voice);
		bool Owns(VoiceHandle voice) const;

		World& m_World;
		AudioDevice* m_Device = nullptr;
		std::weak_ptr<void> m_DeviceLifetime;
		std::thread::id m_Thread = std::this_thread::get_id();
		uint64 m_Context = 0;
		bool m_Playing = false;
		std::vector<Source> m_Sources;
		std::vector<Voice> m_Voices;
		TransformComponent m_Listener;
		Entity m_ListenerEntity = NullEntity;
		std::vector<Entity> m_ListenerCandidates;
		bool m_WarnedNoListener = false;
	};
}
