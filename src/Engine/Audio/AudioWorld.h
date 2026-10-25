#pragma once

#include "Engine/Audio/AudioTypes.h"
#include "Engine/World.h"

#include <vector>
#include <thread>

namespace FP
{
	class AudioDevice;
	struct AudioDebugRay { Vec3 From, To; bool Reflection = false; };
	struct AudioDebugHit
	{
		Entity Object = NullEntity;
		String Material;
		Vec3 Point;
		float Transmission = 1, Absorption = 0, Damping = 0;
		bool Reflection = false;
	};
	struct AudioDebugSource
	{
		uint64 Key = 0;
		Entity Owner = NullEntity;
		Vec3 Listener, Position;
		double Updated = 0;
		float Transmission = 1;
		AudioPlaybackParams Params;
		std::vector<AudioDebugRay> Rays;
		std::vector<AudioDebugHit> Hits;
	};

	struct AudioSpatialGains { float Left = 0; float Right = 0; };
	AudioSpatialGains CalculateAudioSpatialGains(Vec3 source, const TransformComponent& listener,
		float minDistance, float maxDistance);
	float CalculateAudioDoppler(Vec3 source, Vec3 listener, Vec3 sourceVelocity, Vec3 listenerVelocity, float scale = 1);

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
		virtual void SetPaused(bool paused);
		virtual void Update();
		virtual AudioResult Preload(const String& clip);
		virtual AudioPlayResult Play(Entity source);
		virtual AudioPlayResult PlayOneShot(const String& clip, const OneShotParams& params = {});
		// Main thread; callers cache occlusion queries and refresh at most 20 times per second.
		virtual AudioPlaybackParams GetSpatialParameters(const OneShotParams& params);
		virtual bool Stop(VoiceHandle voice);
		virtual bool Pause(VoiceHandle voice);
		virtual bool Resume(VoiceHandle voice);
		virtual VoiceState GetState(VoiceHandle voice) const;
		virtual VoiceHandle GetSourceVoice(Entity source) const;
		virtual void StopSource(Entity source);
		virtual void StopAll();
		virtual Entity GetListenerEntity() const { return m_ListenerEntity; }
		void SetDebugEnabled(bool enabled);
		bool IsDebugEnabled() const { return m_DebugEnabled; }
		const std::vector<AudioDebugSource>& GetDebugSources();

	private:
		friend class World;
		struct Source
		{
			Entity Owner = NullEntity;
			VoiceHandle Voice;
			String RequestedClip;
			bool Recover = false;
			bool Paused = false;
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
			bool Occlusion = true;
			double NextOcclusionCheck = 0;
			float OcclusionGain = 1;
			float LowPassHz = 0;
			AudioRoomParams Room;
			float BasePitch = 1, DopplerScale = 1, DopplerPitch = 1;
			double DopplerTime = 0;
		};
		struct Motion
		{
			Entity Owner = NullEntity;
			Vec3 Previous, Position;
			double PreviousTime = 0, Time = 0;
		};
		Vec3 SampleVelocity(Entity owner, Vec3 position);
		AudioPlayResult PlayInternal(Entity source, bool preservePause);
		Source& TrackSource(Entity source);
		void ResetScene();
		void ConnectRegistry();
		void DisconnectRegistry();
		void SourceRemoved(entt::registry&, Entity entity);
		void SelectListener();
		void ApplySpatial(Voice& voice);
		AudioDebugSource* BeginDebug(Entity owner, Vec3 position, uint64 key);
		void BindDebugVoice(Entity owner, uint64 key);
		void ApplyOcclusion(AudioPlaybackParams& params, Vec3 source, Entity owner, AudioDebugSource* debug = nullptr);
		void ApplyRoom(AudioPlaybackParams& params, Vec3 source, Entity owner, float minDistance, float maxDistance, AudioDebugSource* debug = nullptr);
		Entity ColliderOwner(Entity entity) const;
		bool Owns(VoiceHandle voice) const;

		World& m_World;
		AudioDevice* m_Device = nullptr;
		std::weak_ptr<void> m_DeviceLifetime;
		std::thread::id m_Thread = std::this_thread::get_id();
		uint64 m_Context = 0;
		uint64 m_OutputGeneration = 0;
		bool m_Playing = false;
		bool m_Paused = false;
		std::vector<VoiceHandle> m_PausedForWorld;
		std::vector<Source> m_Sources;
		std::vector<Voice> m_Voices;
		TransformComponent m_Listener;
		Entity m_ListenerEntity = NullEntity;
		std::vector<Entity> m_ListenerCandidates;
		bool m_WarnedNoListener = false;
		bool m_DebugEnabled = false;
		std::vector<AudioDebugSource> m_DebugSources;
		std::vector<Motion> m_Motion;
		Entity m_MotionListener = NullEntity;
	};
}
