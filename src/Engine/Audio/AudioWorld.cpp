#include "Engine/Audio/AudioWorld.h"
#include "Engine/Audio/AudioDevice.h"
#include "Engine/Audio/AudioClip.h"
#include "Engine/Physics/Physics.h"
#include "Engine/Net/NetworkComponent.h"
#include "HAL/PlatformMisc.h"

#include <algorithm>
#include <cmath>

namespace FP
{
	namespace
	{
		bool Finite(Vec3 value)
		{
			return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
		}
		bool ValidDistances(float minDistance, float maxDistance)
		{
			return std::isfinite(minDistance) && std::isfinite(maxDistance) && minDistance > 0 && maxDistance > minDistance;
		}
		bool ValidParams(const AudioPlaybackParams& p)
		{
			return std::isfinite(p.Volume) && p.Volume >= 0 && p.Volume <= 1 && std::isfinite(p.Pitch)
				&& p.Pitch >= 0.25f && p.Pitch <= 4 && p.Priority >= 0 && p.Priority <= 255
				&& (p.Bus == AudioBus::SFX || p.Bus == AudioBus::Music || p.Bus == AudioBus::UI || p.Bus == AudioBus::Voice);
		}
		bool SourceParams(const AudioSourceComponent& source, AudioPlaybackParams& p)
		{
			p.Volume = source.Volume;
			p.Pitch = source.Pitch;
			p.Loop = source.Loop;
			p.Priority = source.Priority;
			if (source.Bus == "SFX") p.Bus = AudioBus::SFX;
			else if (source.Bus == "Music") p.Bus = AudioBus::Music;
			else if (source.Bus == "UI") p.Bus = AudioBus::UI;
			else if (source.Bus == "Voice") p.Bus = AudioBus::Voice;
			else return false;
			return ValidParams(p) && ValidDistances(source.MinDistance, source.MaxDistance);
		}
		AudioPlayResult Failure(AudioError error, const char* message)
		{
			return { {}, { error, message } };
		}
	}

	AudioSpatialGains CalculateAudioSpatialGains(Vec3 source, const TransformComponent& listener,
		float minDistance, float maxDistance)
	{
		// shortcut: stereo panning has no front/back cues; add HRTF when a game needs them.
		if (!Finite(source) || !Finite(listener.Position) || !Finite(listener.Rotation)
			|| !ValidDistances(minDistance, maxDistance)) return {};
		// Double intermediates prevent finite large world positions from overflowing distance math.
		const double x = double(source.X) - listener.Position.X;
		const double y = double(source.Y) - listener.Position.Y;
		const double z = double(source.Z) - listener.Position.Z;
		const double distance = std::sqrt(x * x + y * y + z * z);
		if (distance >= maxDistance) return {};
		const float attenuation = distance <= minDistance ? 1 : float((minDistance / distance)
			* (maxDistance - distance) / (maxDistance - minDistance));
		const Vec3 right = listener.GetRight();
		if (!Finite(right)) return {};
		const float pan = distance > 0 ? std::clamp(float((x * right.X + y * right.Y + z * right.Z) / distance), -1.f, 1.f) : 0;
		return { attenuation * std::sqrt((1 - pan) * 0.5f), attenuation * std::sqrt((1 + pan) * 0.5f) };
	}

	AudioWorld::AudioWorld(World& world) : m_World(world) { ConnectRegistry(); }
	AudioWorld::~AudioWorld() { EndPlay(); DisconnectRegistry(); }

	void AudioWorld::ConnectRegistry()
	{
		m_World.GetRegistry().on_destroy<AudioSourceComponent>().connect<&AudioWorld::SourceRemoved>(*this);
	}

	void AudioWorld::DisconnectRegistry()
	{
		m_World.GetRegistry().on_destroy<AudioSourceComponent>().disconnect<&AudioWorld::SourceRemoved>(*this);
	}

	void AudioWorld::SourceRemoved(entt::registry&, Entity entity)
	{
		StopSource(entity);
		std::erase_if(m_Sources, [entity](const Source& source) { return source.Owner == entity; });
	}

	bool AudioWorld::Attach(AudioDevice* device)
	{
		if (m_Playing) return false;
		StopAll();
		m_Device = device;
		m_DeviceLifetime = device ? device->GetLifetimeToken() : std::weak_ptr<void>{};
		return true;
	}

	AudioDevice* AudioWorld::GetDevice() const
	{
		FP_ASSERT(std::this_thread::get_id() == m_Thread, "AudioWorld is main-thread only");
		return m_DeviceLifetime.expired() ? nullptr : m_Device;
	}

	AudioResult AudioWorld::Preload(const String& clip)
	{
		if (!GetDevice()) return { AudioError::NotReady, "No audio service is attached" };
		return m_Device->GetClipCache().Load(clip).Result;
	}

	void AudioWorld::BeginPlay()
	{
		if (m_Playing) return;
		ResetScene();
		m_Playing = true;
		m_Context = AllocateAudioContext();
		if (GetDevice()) m_Device->GetClipCache().ClearFailures();
		for (auto [entity, source] : m_World.View<AudioSourceComponent>().each())
			if (source.Enabled && !source.Clip.IsEmpty()) Preload(source.Clip);
		Update();
	}

	void AudioWorld::EndPlay()
	{
		StopAll();
		m_Playing = false;
		m_Context = 0;
		m_Sources.clear();
	}

	void AudioWorld::StopAll()
	{
		if (GetDevice() && m_Context) m_Device->StopContext(m_Context);
		m_Voices.clear();
		for (Source& source : m_Sources) source.Voice = {};
		m_Context = m_Playing ? AllocateAudioContext() : 0;
	}

	void AudioWorld::ResetScene()
	{
		StopAll();
		m_Sources.clear();
		m_ListenerEntity = NullEntity;
		m_ListenerCandidates.clear();
		m_WarnedNoListener = false;
	}

	AudioWorld::Source& AudioWorld::TrackSource(Entity entity)
	{
		for (Source& source : m_Sources) if (source.Owner == entity) return source;
		m_Sources.push_back({ entity });
		return m_Sources.back();
	}

	AudioPlayResult AudioWorld::Play(Entity entity)
	{
		return PlayInternal(entity, false);
	}

	AudioPlayResult AudioWorld::PlayInternal(Entity entity, bool preservePause)
	{
		if (!m_World.IsValid(entity)) return Failure(AudioError::InvalidSource, "Audio source entity is invalid");
		const auto* source = m_World.TryGet<AudioSourceComponent>(entity);
		if (!source || !source->Enabled) return Failure(AudioError::InvalidSource, "Audio source is absent or disabled");
		if (!m_Playing || !GetDevice() || !m_Device->IsReady()) return Failure(AudioError::NotReady, "Audio world is stopped or output is unavailable");
		Source& tracked = TrackSource(entity);
		tracked.RequestedClip = source->Clip;
		AudioPlaybackParams params;
		if (!SourceParams(*source, params)) return Failure(AudioError::InvalidSettings, "Invalid audio volume, pitch, bus, priority or distance");
		const auto loaded = m_Device->GetClipCache().Load(source->Clip);
		if (!loaded.Result) return { {}, loaded.Result };
		if (source->Spatial && loaded.Clip->Channels != 1)
			return Failure(AudioError::InvalidSettings, "Spatial playback requires a mono WAV; export a mono version");
		Voice voice{ {}, entity, true, source->Spatial, {}, source->MinDistance, source->MaxDistance, params };
		voice.Channels = loaded.Clip->Channels;
		voice.Occlusion = source->Occlusion;
		SelectListener();
		ApplySpatial(voice);
		const VoiceHandle previous = tracked.Voice;
		const bool paused = preservePause && GetState(previous) == VoiceState::Paused;
		auto result = m_Device->Play(m_Context, loaded.Clip, voice.Params, previous, paused);
		if (!result) return result;
		std::erase_if(m_Voices, [previous](const Voice& v) { return v.Handle == previous; });
		voice.Handle = result.Voice;
		m_Voices.push_back(voice);
		tracked.Voice = result.Voice;
		return result;
	}

	AudioPlayResult AudioWorld::PlayOneShot(const String& clip, const OneShotParams& p)
	{
		if (!m_Playing || !GetDevice() || !m_Device->IsReady()) return Failure(AudioError::NotReady, "Audio world is stopped or output is unavailable");
		if (p.Owner != NullEntity && !m_World.IsValid(p.Owner)) return Failure(AudioError::InvalidSource, "One-shot owner entity is invalid");
		if (p.Owner != NullEntity)
			if (const auto* source = m_World.TryGet<AudioSourceComponent>(p.Owner); source && !source->Enabled)
				return Failure(AudioError::InvalidSource, "One-shot owner audio source is disabled");
		AudioPlaybackParams params;
		params.Volume = p.Volume; params.Pitch = p.Pitch; params.Bus = p.Bus; params.Priority = p.Priority;
		if (!ValidParams(params) || !ValidDistances(p.MinDistance, p.MaxDistance) || !Finite(p.Position))
			return Failure(AudioError::InvalidSettings, "Invalid one-shot playback settings");
		const auto loaded = m_Device->GetClipCache().Load(clip);
		if (!loaded.Result) return { {}, loaded.Result };
		if (p.Spatial && loaded.Clip->Channels != 1)
			return Failure(AudioError::InvalidSettings, "Spatial playback requires a mono WAV; export a mono version");
		Voice voice{ {}, p.Owner, false, p.Spatial, p.Position, p.MinDistance, p.MaxDistance, params };
		voice.Occlusion = p.Occlusion;
		SelectListener();
		ApplySpatial(voice);
		auto result = m_Device->Play(m_Context, loaded.Clip, voice.Params);
		if (result) { voice.Handle = result.Voice; m_Voices.push_back(voice); }
		return result;
	}

	AudioPlaybackParams AudioWorld::GetSpatialParameters(const OneShotParams& p)
	{
		FP_ASSERT(std::this_thread::get_id() == m_Thread, "AudioWorld is main-thread only");
		AudioPlaybackParams params;
		params.Volume = p.Volume; params.Pitch = p.Pitch; params.Bus = p.Bus; params.Priority = p.Priority;
		auto silent = []() { AudioPlaybackParams result; result.LeftGain = result.RightGain = 0; return result; };
		if (!ValidParams(params) || !ValidDistances(p.MinDistance, p.MaxDistance) || !Finite(p.Position)
			|| (p.Owner != NullEntity && !m_World.IsValid(p.Owner))) return silent();
		if (!p.Spatial) return params;
		SelectListener();
		Vec3 position = p.Position;
		if (p.Owner != NullEntity)
		{
			const auto* transform = m_World.TryGet<TransformComponent>(p.Owner);
			if (!transform || !Finite(transform->Position) || !Finite(transform->Rotation)) return silent();
			position = m_World.GetWorldPosition(p.Owner);
		}
		const auto gains = CalculateAudioSpatialGains(position, m_Listener, p.MinDistance, p.MaxDistance);
		params.LeftGain = gains.Left; params.RightGain = gains.Right;
		if (p.Occlusion) ApplyOcclusion(params, position, p.Owner);
		return params;
	}

	bool AudioWorld::Owns(VoiceHandle voice) const
	{
		return m_Playing && GetDevice() && voice.Context == m_Context && bool(voice);
	}
	bool AudioWorld::Stop(VoiceHandle voice) { return Owns(voice) && m_Device->Stop(voice); }
	bool AudioWorld::Pause(VoiceHandle voice) { return Owns(voice) && m_Device->Pause(voice); }
	bool AudioWorld::Resume(VoiceHandle voice) { return Owns(voice) && m_Device->Resume(voice); }
	VoiceState AudioWorld::GetState(VoiceHandle voice) const { return Owns(voice) ? m_Device->GetState(voice) : VoiceState::Invalid; }
	VoiceHandle AudioWorld::GetSourceVoice(Entity entity) const
	{
		for (const Source& source : m_Sources)
			if (source.Owner == entity && GetState(source.Voice) != VoiceState::Invalid) return source.Voice;
		return {};
	}

	void AudioWorld::StopSource(Entity entity)
	{
		for (const Voice& voice : m_Voices) if (voice.Owner == entity) Stop(voice.Handle);
		std::erase_if(m_Voices, [entity](const Voice& voice) { return voice.Owner == entity; });
		for (Source& source : m_Sources) if (source.Owner == entity) source.Voice = {};
	}

	void AudioWorld::SelectListener()
	{
		std::vector<Entity> candidates;
		const auto entities = m_World.GetEntities();
		auto validTransform = [this](Entity entity) {
			if (const auto* network = m_World.TryGet<NetworkComponent>(entity); network && network->Remote) return false;
			const auto* t = m_World.TryGet<TransformComponent>(entity);
			return t && Finite(t->Position) && Finite(t->Rotation);
		};
		for (Entity entity : entities)
			if (const auto* listener = m_World.TryGet<AudioListenerComponent>(entity); listener && listener->Enabled && validTransform(entity))
				candidates.push_back(entity);
		if (candidates.empty())
			for (Entity entity : entities)
				if (const auto* camera = m_World.TryGet<CameraComponent>(entity); camera && camera->Primary && validTransform(entity))
					candidates.push_back(entity);
		if (candidates != m_ListenerCandidates && candidates.size() > 1)
			FP_LOG_WARN("Multiple audio listeners/cameras: using the earliest created eligible entity");
		m_ListenerCandidates = std::move(candidates);
		m_ListenerEntity = m_ListenerCandidates.empty() ? Entity(NullEntity) : m_ListenerCandidates.front();
		m_Listener = m_ListenerEntity == NullEntity ? TransformComponent{} : m_World.GetWorldTransform(m_ListenerEntity);
	}

	void AudioWorld::ApplySpatial(Voice& voice)
	{
		voice.Params.LeftGain = voice.Params.RightGain = 1;
		voice.Params.LowPassHz = 0;
		if (!voice.Spatial)
		{
			voice.NextOcclusionCheck = 0; voice.OcclusionGain = 1; voice.LowPassHz = 0;
			return;
		}
		Vec3 position = voice.Position;
		if (voice.Owner != NullEntity)
		{
			const auto* transform = m_World.TryGet<TransformComponent>(voice.Owner);
			if (!transform || !Finite(transform->Position) || !Finite(transform->Rotation))
			{
				voice.Params.LeftGain = voice.Params.RightGain = 0;
				return;
			}
			position = m_World.GetWorldPosition(voice.Owner);
		}
		const auto gains = CalculateAudioSpatialGains(position, m_Listener, voice.MinDistance, voice.MaxDistance);
		voice.Params.LeftGain = gains.Left; voice.Params.RightGain = gains.Right;
		if (voice.Occlusion)
		{
			const double now = PlatformMisc::GetTimeSeconds();
			if (now >= voice.NextOcclusionCheck)
			{
				AudioPlaybackParams occlusion;
				if (gains.Left > 0 || gains.Right > 0) ApplyOcclusion(occlusion, position, voice.Owner);
				voice.OcclusionGain = occlusion.LeftGain;
				voice.LowPassHz = occlusion.LowPassHz;
				voice.NextOcclusionCheck = now + 0.05;
			}
			voice.Params.LeftGain *= voice.OcclusionGain; voice.Params.RightGain *= voice.OcclusionGain;
			voice.Params.LowPassHz = voice.LowPassHz;
		}
		else
		{
			voice.NextOcclusionCheck = 0; voice.OcclusionGain = 1; voice.LowPassHz = 0;
		}
		if (m_ListenerEntity == NullEntity && !m_WarnedNoListener)
		{
			FP_LOG_WARN("Spatial audio has no listener or primary camera; using the origin");
			m_WarnedNoListener = true;
		}
	}

	void AudioWorld::ApplyOcclusion(AudioPlaybackParams& params, Vec3 source, Entity owner)
	{
		if ((params.LeftGain == 0 && params.RightGain == 0) || !Finite(source) || !Finite(m_Listener.Position)) return;
		if (m_World.View<ColliderComponent>().empty()) return;
		const double x = double(source.X) - m_Listener.Position.X;
		const double y = double(source.Y) - m_Listener.Position.Y;
		const double z = double(source.Z) - m_Listener.Position.Z;
		const double distance = std::sqrt(x * x + y * y + z * z);
		if (distance < 0.001 || distance > std::numeric_limits<float>::max()) return;
		auto colliderOwner = [this](Entity entity) {
			for (int32 depth = 0; m_World.IsValid(entity) && depth < 1000; ++depth)
			{
				if (const auto* collider = m_World.TryGet<ColliderComponent>(entity); collider && !collider->Trigger) return entity;
				entity = m_World.GetParent(entity);
			}
			return Entity(NullEntity);
		};
		RaycastHit hit;
		if (!m_World.GetPhysics().Raycast(m_Listener.Position,
			{ float(x / distance), float(y / distance), float(z / distance) }, float(distance), hit, colliderOwner(m_ListenerEntity), colliderOwner(owner))) return;
		// shortcut: one solid hit applies a fixed muffling target; add material/thickness transmission when needed.
		params.LeftGain *= 0.25f; params.RightGain *= 0.25f;
		params.LowPassHz = 1200;
	}

	void AudioWorld::Update()
	{
		if (!m_Playing) return;
		SelectListener();
		auto report = [this](Entity entity, const AudioPlayResult& result) {
			if (!result) FP_LOG_WARN("Audio source '%s': %s", m_World.GetEntityName(entity).CStr(), result.Result.Message.CStr());
		};
		for (Entity entity : m_World.GetEntities())
		{
			const auto* source = m_World.TryGet<AudioSourceComponent>(entity);
			if (!source) continue;
			const bool added = std::none_of(m_Sources.begin(), m_Sources.end(), [entity](const Source& s) { return s.Owner == entity; });
			Source& tracked = TrackSource(entity);
			if (added)
			{
				tracked.RequestedClip = source->Clip;
				if (!source->Enabled) StopSource(entity);
				else if (source->PlayOnStart) report(entity, Play(entity));
			}
			else if (!source->Enabled) StopSource(entity);
			else if (GetState(tracked.Voice) != VoiceState::Invalid && source->Clip != tracked.RequestedClip)
			{
				tracked.RequestedClip = source->Clip;
				report(entity, PlayInternal(entity, true));
			}
		}
		std::erase_if(m_Sources, [this](const Source& source) { return !m_World.IsValid(source.Owner); });
		for (Voice& voice : m_Voices)
		{
			if (voice.Owner != NullEntity && !m_World.IsValid(voice.Owner)) { Stop(voice.Handle); continue; }
			if (GetState(voice.Handle) == VoiceState::Invalid) continue;
			if (voice.Primary)
			{
				const auto* source = m_World.TryGet<AudioSourceComponent>(voice.Owner);
				AudioPlaybackParams params;
				if (!source || !SourceParams(*source, params) || (source->Spatial && voice.Channels != 1))
				{
					if (voice.SettingsValid) FP_LOG_WARN("Audio source '%s': invalid live settings; voice is muted until corrected", m_World.GetEntityName(voice.Owner).CStr());
					voice.SettingsValid = false;
					voice.Params.LeftGain = voice.Params.RightGain = 0;
					if (GetDevice()) m_Device->SetParameters(voice.Handle, voice.Params);
					continue;
				}
				voice.SettingsValid = true;
				voice.Params = params;
				voice.Spatial = source->Spatial;
				voice.Occlusion = source->Occlusion;
				voice.MinDistance = source->MinDistance; voice.MaxDistance = source->MaxDistance;
			}
			ApplySpatial(voice);
			if (GetDevice()) m_Device->SetParameters(voice.Handle, voice.Params);
		}
		std::erase_if(m_Voices, [this](const Voice& voice) { return GetState(voice.Handle) == VoiceState::Invalid; });
	}
}
