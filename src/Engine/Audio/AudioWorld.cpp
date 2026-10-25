#include "Engine/Audio/AudioWorld.h"
#include "Engine/Audio/AudioDevice.h"
#include "Engine/Audio/AudioClip.h"
#include "Engine/Physics/Physics.h"
#include "Engine/Net/NetworkComponent.h"
#include "Engine/Material.h"
#include "Engine/Paths.h"
#include "HAL/PlatformMisc.h"

#include <algorithm>
#include <cmath>

namespace FP
{
	namespace
	{
		struct SoundSurface
		{
			float Transmission = 0.25f, Absorption = 0.4f, Damping = 0.5f;
			const Material* Asset = nullptr;
		};
		SoundSurface SurfaceSound(World& world, Entity entity)
		{
			for (int depth = 0; world.IsValid(entity) && depth < 1000; ++depth, entity = world.GetParent(entity))
			{
				const auto* component = world.TryGet<MaterialComponent>(entity);
				if (!component || component->GetForSection(0).IsEmpty()) continue;
				// shortcut: physics identifies the object, not its mesh section; use slot zero until hit-section mapping exists.
				const auto* material = MaterialLibrary::Get().Load(component->GetForSection(0));
				if (!material) return {};
				const auto& data = material->GetData();
				auto scalar = [](float value, float fallback) { return std::isfinite(value) ? std::clamp(value, 0.f, 1.f) : fallback; };
				return {scalar(data.SoundTransmission, 0.25f), scalar(data.SoundAbsorption, 0.4f), scalar(data.SoundDamping, 0.5f), material};
			}
			return {};
		}
		void RecordHit(AudioDebugSource* debug, const RaycastHit& hit, const SoundSurface& surface, bool reflection)
		{
			if (debug && debug->Hits.size() < 64)
				debug->Hits.push_back({hit.Entity, surface.Asset ? Paths::AssetPath(surface.Asset->GetPath()) : String("Default"), hit.Point,
					surface.Transmission, surface.Absorption, surface.Damping, reflection});
		}
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
			return ValidParams(p) && ValidDistances(source.MinDistance, source.MaxDistance)
				&& std::isfinite(source.DopplerScale) && source.DopplerScale >= 0 && source.DopplerScale <= 4;
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

	float CalculateAudioDoppler(Vec3 source, Vec3 listener, Vec3 sourceVelocity, Vec3 listenerVelocity, float scale)
	{
		if (!Finite(source) || !Finite(listener) || !Finite(sourceVelocity) || !Finite(listenerVelocity)
			|| !std::isfinite(scale) || scale <= 0 || scale > 4) return 1;
		const double x = double(source.X) - listener.X, y = double(source.Y) - listener.Y, z = double(source.Z) - listener.Z;
		const double distance = std::sqrt(x * x + y * y + z * z);
		if (distance < 0.01) return 1;
		constexpr double speedOfSound = 343;
		auto radial = [&](Vec3 velocity) { return std::clamp((x * velocity.X + y * velocity.Y + z * velocity.Z) * scale / distance, -0.9 * speedOfSound, 0.9 * speedOfSound); };
		return float(std::clamp((speedOfSound + radial(listenerVelocity)) / (speedOfSound + radial(sourceVelocity)), 0.5, 2.0));
	}

	Vec3 AudioWorld::SampleVelocity(Entity owner, Vec3 position)
	{
		if (!m_Playing || !m_World.IsValid(owner) || !Finite(position)) return {};
		const double now = m_World.GetTime();
		Motion* motion = nullptr;
		for (auto& sample : m_Motion) if (sample.Owner == owner) { motion = &sample; break; }
		if (!motion)
		{
			if (m_Motion.size() == 256)
				m_Motion.erase(std::min_element(m_Motion.begin(), m_Motion.end(), [](const auto& a, const auto& b) { return a.Time < b.Time; }));
			m_Motion.push_back({owner, position, position, now, now});
			return {};
		}
		if (now < motion->Time) { *motion = {owner, position, position, now, now}; return {}; }
		if (now > motion->Time)
		{
			motion->Previous = motion->Position; motion->PreviousTime = motion->Time; motion->Time = now;
		}
		motion->Position = position;
		const double delta = now - motion->PreviousTime;
		if (delta <= 0.000001 || delta > 0.25) return {};
		const Vec3 displacement = position - motion->Previous;
		const Vec3 velocity = displacement / float(delta);
		// shortcut: jumps over 20 m or 300 m/s count as teleports; explicit velocity/teleport APIs can replace this heuristic.
		if (!Finite(velocity) || displacement.LengthSquared() > 400 || velocity.LengthSquared() > 90000) return {};
		return velocity;
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
		m_Paused = false;
		m_Context = AllocateAudioContext();
		if (AudioDevice* device = GetDevice())
		{
			device->GetClipCache().ClearFailures();
			for (auto [entity, source] : m_World.View<AudioSourceComponent>().each())
				if (source.Enabled && !source.Clip.IsEmpty()) device->GetClipCache().Load(source.Clip, false, source.Spatial);
		}
		Update();
	}

	void AudioWorld::EndPlay()
	{
		m_Motion.clear(); m_MotionListener = NullEntity;
		m_DebugSources.clear();
		StopAll();
		m_Playing = false;
		m_Paused = false;
		m_Context = 0;
		m_Sources.clear();
	}

	void AudioWorld::SetPaused(bool paused)
	{
		if (!m_Playing || m_Paused == paused) return;
		m_Paused = paused;
		if (paused)
		{
			m_PausedForWorld.clear();
			if (!GetDevice()) return;
			for (const Voice& voice : m_Voices)
			{
				const VoiceState state = m_Device->GetState(voice.Handle);
				if ((state == VoiceState::Playing || state == VoiceState::Draining) && m_Device->Pause(voice.Handle))
					m_PausedForWorld.push_back(voice.Handle);
			}
		}
		else
		{
			if (GetDevice())
				for (VoiceHandle voice : m_PausedForWorld)
					if (m_Device->GetState(voice) == VoiceState::Paused) m_Device->Resume(voice);
			m_PausedForWorld.clear();
		}
	}

	void AudioWorld::StopAll()
	{
		if (GetDevice() && m_Context) m_Device->StopContext(m_Context);
		m_Voices.clear();
		m_PausedForWorld.clear();
		for (Source& source : m_Sources) { source.Voice = {}; source.Recover = source.Paused = false; }
		m_Context = m_Playing ? AllocateAudioContext() : 0;
	}

	void AudioWorld::ResetScene()
	{
		m_Motion.clear(); m_MotionListener = NullEntity;
		m_DebugSources.clear();
		StopAll();
		m_Sources.clear();
		m_OutputGeneration = 0;
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
		tracked.RequestedSpatial = source->Spatial;
		AudioPlaybackParams params;
		if (!SourceParams(*source, params)) return Failure(AudioError::InvalidSettings, "Invalid audio volume, pitch, bus, priority or distance");
		const auto loaded = m_Device->GetClipCache().Load(source->Clip, false, source->Spatial);
		if (!loaded.Result) return { {}, loaded.Result };
		Voice voice{ {}, entity, true, source->Spatial, {}, source->MinDistance, source->MaxDistance, params };
		voice.Channels = loaded.Clip->Channels;
		voice.Occlusion = source->Occlusion;
		voice.BasePitch = params.Pitch; voice.DopplerScale = source->DopplerScale;
		SelectListener();
		ApplySpatial(voice);
		const VoiceHandle previous = tracked.Voice;
		const bool previouslyPaused = GetState(previous) == VoiceState::Paused || (preservePause && tracked.Paused);
		const bool pauseWithWorld = m_Paused && !(preservePause && previouslyPaused
			&& std::find(m_PausedForWorld.begin(), m_PausedForWorld.end(), previous) == m_PausedForWorld.end());
		auto result = m_Device->Play(m_Context, loaded.Clip, voice.Params, previous, m_Paused || (preservePause && previouslyPaused));
		if (!result) return result;
		std::erase(m_PausedForWorld, previous);
		if (pauseWithWorld) m_PausedForWorld.push_back(result.Voice);
		std::erase_if(m_Voices, [previous](const Voice& v) { return v.Handle == previous; });
		voice.Handle = result.Voice;
		BindDebugVoice(voice.Owner, result.Voice.Generation);
		m_Voices.push_back(voice);
		tracked.Voice = result.Voice;
		tracked.Recover = params.Loop;
		tracked.Paused = preservePause && tracked.Paused;
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
		if (!ValidParams(params) || !ValidDistances(p.MinDistance, p.MaxDistance) || !Finite(p.Position)
			|| !std::isfinite(p.DopplerScale) || p.DopplerScale < 0 || p.DopplerScale > 4)
			return Failure(AudioError::InvalidSettings, "Invalid one-shot playback settings");
		const auto loaded = m_Device->GetClipCache().Load(clip, false, p.Spatial);
		if (!loaded.Result) return { {}, loaded.Result };
		Voice voice{ {}, p.Owner, false, p.Spatial, p.Position, p.MinDistance, p.MaxDistance, params };
		voice.Channels = loaded.Clip->Channels;
		voice.Occlusion = p.Occlusion;
		voice.BasePitch = p.Pitch; voice.DopplerScale = p.DopplerScale;
		SelectListener();
		ApplySpatial(voice);
		auto result = m_Device->Play(m_Context, loaded.Clip, voice.Params, {}, m_Paused);
		if (result)
		{
			voice.Handle = result.Voice;
			BindDebugVoice(voice.Owner, result.Voice.Generation);
			m_Voices.push_back(voice);
			if (m_Paused) m_PausedForWorld.push_back(result.Voice);
		}
		return result;
	}

	AudioPlaybackParams AudioWorld::GetSpatialParameters(const OneShotParams& p)
	{
		FP_ASSERT(std::this_thread::get_id() == m_Thread, "AudioWorld is main-thread only");
		AudioPlaybackParams params;
		params.Volume = p.Volume; params.Pitch = p.Pitch; params.Bus = p.Bus; params.Priority = p.Priority;
		auto silent = []() { AudioPlaybackParams result; result.LeftGain = result.RightGain = 0; return result; };
		if (!ValidParams(params) || !ValidDistances(p.MinDistance, p.MaxDistance) || !Finite(p.Position)
			|| !std::isfinite(p.DopplerScale) || p.DopplerScale < 0 || p.DopplerScale > 4
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
		const Vec3 sourceVelocity = SampleVelocity(p.Owner, position);
		const Vec3 listenerVelocity = SampleVelocity(m_ListenerEntity, m_Listener.Position);
		params.Pitch = std::clamp(p.Pitch * CalculateAudioDoppler(position, m_Listener.Position, sourceVelocity, listenerVelocity, p.DopplerScale), 0.25f, 4.f);
		auto* debug = BeginDebug(p.Owner, position, 0);
		if (p.Occlusion) ApplyOcclusion(params, position, p.Owner, debug);
		ApplyRoom(params, position, p.Owner, p.MinDistance, p.MaxDistance, debug);
		if (debug) debug->Params = params;
		return params;
	}

	bool AudioWorld::Owns(VoiceHandle voice) const
	{
		return m_Playing && GetDevice() && voice.Context == m_Context && bool(voice);
	}
	bool AudioWorld::Stop(VoiceHandle voice)
	{
		if (!Owns(voice) || !m_Device->Stop(voice)) return false;
		for (Source& source : m_Sources) if (source.Voice == voice) source.Recover = source.Paused = false;
		return true;
	}
	bool AudioWorld::Pause(VoiceHandle voice)
	{
		if (!Owns(voice) || !m_Device->Pause(voice)) return false;
		// An explicit pause remains in effect after the world resumes.
		std::erase(m_PausedForWorld, voice);
		for (Source& source : m_Sources) if (source.Voice == voice) source.Paused = true;
		return true;
	}
	bool AudioWorld::Resume(VoiceHandle voice)
	{
		if (m_Paused || !Owns(voice) || !m_Device->Resume(voice)) return false;
		for (Source& source : m_Sources) if (source.Voice == voice) source.Paused = false;
		return true;
	}
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
		for (Source& source : m_Sources) if (source.Owner == entity) { source.Voice = {}; source.Recover = source.Paused = false; }
	}

	void AudioWorld::SelectListener()
	{
		std::vector<Entity> candidates;
		auto validTransform = [this](Entity entity) {
			if (const auto* network = m_World.TryGet<NetworkComponent>(entity); network && network->Remote) return false;
			const auto* t = m_World.TryGet<TransformComponent>(entity);
			return t && Finite(t->Position) && Finite(t->Rotation);
		};
		for (auto [entity, listener] : m_World.View<AudioListenerComponent>().each())
			if (listener.Enabled && validTransform(entity))
				candidates.push_back(entity);
		if (candidates.empty())
			for (auto [entity, camera] : m_World.View<CameraComponent>().each())
				if (camera.Primary && validTransform(entity))
					candidates.push_back(entity);
		std::sort(candidates.begin(), candidates.end(), [this](Entity a, Entity b) { return m_World.GetCreationIndex(a) < m_World.GetCreationIndex(b); });
		if (candidates != m_ListenerCandidates && candidates.size() > 1)
			FP_LOG_WARN("Multiple audio listeners/cameras: using the earliest created eligible entity");
		m_ListenerCandidates = std::move(candidates);
		m_ListenerEntity = m_ListenerCandidates.empty() ? Entity(NullEntity) : m_ListenerCandidates.front();
		if (m_ListenerEntity != m_MotionListener)
		{
			std::erase_if(m_Motion, [this](const auto& sample) { return sample.Owner == m_ListenerEntity; });
			m_MotionListener = m_ListenerEntity;
		}
		m_Listener = m_ListenerEntity == NullEntity ? TransformComponent{} : m_World.GetWorldTransform(m_ListenerEntity);
	}

	void AudioWorld::ApplySpatial(Voice& voice)
	{
		voice.Params.Pitch = voice.BasePitch;
		voice.Params.LeftGain = voice.Params.RightGain = 1;
		voice.Params.LowPassHz = 0;
		voice.Params.Room = {};
		if (!voice.Spatial)
		{
			voice.DopplerPitch = 1;
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
		const Vec3 sourceVelocity = SampleVelocity(voice.Owner, position);
		const Vec3 listenerVelocity = SampleVelocity(m_ListenerEntity, m_Listener.Position);
		const float targetPitch = CalculateAudioDoppler(position, m_Listener.Position, sourceVelocity, listenerVelocity, voice.DopplerScale);
		const double frameTime = m_World.GetTime();
		const double elapsed = std::clamp(frameTime - voice.DopplerTime, 0.0, 0.1);
		voice.DopplerTime = frameTime;
		if (voice.DopplerScale == 0) voice.DopplerPitch = 1;
		else voice.DopplerPitch += (targetPitch - voice.DopplerPitch) * float(1 - std::exp(-elapsed / 0.05));
		voice.Params.Pitch = std::clamp(voice.BasePitch * voice.DopplerPitch, 0.25f, 4.f);
		const double now = PlatformMisc::GetTimeSeconds();
		if (now >= voice.NextOcclusionCheck)
		{
			AudioPlaybackParams effects;
			auto* debug = BeginDebug(voice.Owner, position, voice.Handle.Generation);
			if (gains.Left > 0 || gains.Right > 0)
			{
				if (voice.Occlusion) ApplyOcclusion(effects, position, voice.Owner, debug);
				ApplyRoom(effects, position, voice.Owner, voice.MinDistance, voice.MaxDistance, debug);
			}
			voice.OcclusionGain = effects.LeftGain;
			voice.LowPassHz = effects.LowPassHz;
			voice.Room = effects.Room;
			if (debug)
			{
				debug->Params = voice.Params;
				debug->Params.LeftGain *= effects.LeftGain; debug->Params.RightGain *= effects.RightGain;
				debug->Params.LowPassHz = effects.LowPassHz; debug->Params.Room = effects.Room;
			}
			voice.NextOcclusionCheck = now + 0.05;
		}
		if (voice.Occlusion)
		{
			voice.Params.LeftGain *= voice.OcclusionGain; voice.Params.RightGain *= voice.OcclusionGain;
			voice.Params.LowPassHz = voice.LowPassHz;
		}
		voice.Params.Room = voice.Room;
		if (m_ListenerEntity == NullEntity && !m_WarnedNoListener)
		{
			FP_LOG_WARN("Spatial audio has no listener or primary camera; using the origin");
			m_WarnedNoListener = true;
		}
	}

	void AudioWorld::SetDebugEnabled(bool enabled)
	{
		FP_ASSERT(std::this_thread::get_id() == m_Thread);
		if (m_DebugEnabled == enabled) return;
		m_DebugEnabled = enabled;
		m_DebugSources.clear();
	}
	const std::vector<AudioDebugSource>& AudioWorld::GetDebugSources()
	{
		FP_ASSERT(std::this_thread::get_id() == m_Thread);
		const double now = PlatformMisc::GetTimeSeconds();
		std::erase_if(m_DebugSources, [now](const auto& source) { return now - source.Updated > 0.5; });
		return m_DebugSources;
	}
	AudioDebugSource* AudioWorld::BeginDebug(Entity owner, Vec3 position, uint64 key)
	{
		if (!m_DebugEnabled) return nullptr;
		AudioDebugSource* found = nullptr;
		for (auto& source : m_DebugSources) if (source.Owner == owner && source.Key == key) { found = &source; break; }
		if (!found)
		{
			if (m_DebugSources.size() == 64)
				m_DebugSources.erase(std::min_element(m_DebugSources.begin(), m_DebugSources.end(), [](const auto& a, const auto& b) { return a.Updated < b.Updated; }));
			m_DebugSources.emplace_back(); found = &m_DebugSources.back();
		}
		*found = {};
		found->Owner = owner; found->Key = key; found->Position = position; found->Listener = m_Listener.Position;
		found->Updated = PlatformMisc::GetTimeSeconds();
		found->Rays.push_back({m_Listener.Position, position, false});
		return found;
	}
	void AudioWorld::BindDebugVoice(Entity owner, uint64 key)
	{
		if (!m_DebugEnabled) return;
		for (auto& source : m_DebugSources)
			if (source.Owner == owner && source.Key == 0) { source.Key = key; break; }
	}
	void AudioWorld::ApplyOcclusion(AudioPlaybackParams& params, Vec3 source, Entity owner, AudioDebugSource* debug)
	{
		if ((params.LeftGain == 0 && params.RightGain == 0) || !Finite(source) || !Finite(m_Listener.Position)) return;
		if (m_World.View<ColliderComponent>().empty()) return;
		const double x = double(source.X) - m_Listener.Position.X;
		const double y = double(source.Y) - m_Listener.Position.Y;
		const double z = double(source.Z) - m_Listener.Position.Z;
		const double distance = std::sqrt(x * x + y * y + z * z);
		if (distance < 0.001 || distance > std::numeric_limits<float>::max()) return;
		float transmission = 1, damping = 0;
		const auto hits = m_World.GetPhysics().RaycastAll(m_Listener.Position,
			{float(x / distance), float(y / distance), float(z / distance)}, float(distance), ColliderOwner(m_ListenerEntity), ColliderOwner(owner));
		for (const auto& hit : hits)
		{
			const auto surface = SurfaceSound(m_World, hit.Entity);
			RecordHit(debug, hit, surface, false);
			transmission *= surface.Transmission;
			damping = std::max(damping, surface.Damping);
		}
		params.LeftGain *= transmission; params.RightGain *= transmission;
		params.LowPassHz = AudioDampingCutoff(damping);
		if (debug) debug->Transmission = transmission;
	}

	Entity AudioWorld::ColliderOwner(Entity entity) const
	{
		for (int32 depth = 0; m_World.IsValid(entity) && depth < 1000; ++depth)
		{
			if (const auto* collider = m_World.TryGet<ColliderComponent>(entity); collider && !collider->Trigger) return entity;
			entity = m_World.GetParent(entity);
		}
		return Entity(NullEntity);
	}

	void AudioWorld::ApplyRoom(AudioPlaybackParams& params, Vec3 source, Entity owner, float minDistance, float maxDistance, AudioDebugSource* debug)
	{
		// shortcut: first-order box reflections and a synthetic late tail; use mesh propagation for irregular rooms and portals.
		std::vector<Entity> rooms;
		for (Entity entity : m_World.View<AudioRoomComponent>()) rooms.push_back(entity);
		std::sort(rooms.begin(), rooms.end(), [this](Entity a, Entity b) { return m_World.GetCreationIndex(a) < m_World.GetCreationIndex(b); });
		for (Entity entity : rooms)
		{
			const auto* room = m_World.TryGet<AudioRoomComponent>(entity);
			if (!room || !room->Enabled || !Finite(room->Size) || !std::isfinite(room->Wet) || room->Wet <= 0 || room->Wet > 1
				|| !std::isfinite(room->Decay) || room->Decay < 0.1f || room->Decay > 6) continue;
			const auto transform = m_World.GetWorldTransform(entity);
			if (!Finite(transform.Position) || !Finite(transform.Rotation) || !Finite(transform.Scale)) continue;
			const Vec3 size = room->Size * transform.Scale;
			if (!Finite(size) || size.X < 0.5f || size.Y < 0.5f || size.Z < 0.5f || size.X > 100 || size.Y > 100 || size.Z > 100) continue;
			const auto rotation = transform.GetRotation();
			const Quat inverse{ -rotation.X, -rotation.Y, -rotation.Z, rotation.W };
			const Vec3 listener = inverse.Rotate(m_Listener.Position - transform.Position);
			const Vec3 emitter = inverse.Rotate(source - transform.Position);
			const Vec3 half = size * 0.5f;
			auto inside = [half](Vec3 p) { return std::abs(p.X) < half.X && std::abs(p.Y) < half.Y && std::abs(p.Z) < half.Z; };
			if (!inside(listener) || !inside(emitter)) continue;
			const float distance = (source - m_Listener.Position).Length();
			if (!std::isfinite(distance) || distance >= maxDistance) return;
			const float edge = std::min({half.X - std::abs(listener.X), half.Y - std::abs(listener.Y), half.Z - std::abs(listener.Z),
				half.X - std::abs(emitter.X), half.Y - std::abs(emitter.Y), half.Z - std::abs(emitter.Z)});
			params.Room.Wet = room->Wet * std::clamp(edge / 0.5f, 0.f, 1.f);
			params.Room.Decay = room->Decay;
			const auto direct = CalculateAudioSpatialGains(source, m_Listener, minDistance, maxDistance);
			params.Room.LateGain = std::sqrt(direct.Left * direct.Left + direct.Right * direct.Right) * 0.6f;
			const Entity ignore = ColliderOwner(owner), ignoreAlso = ColliderOwner(m_ListenerEntity);
			const float positions[] = {emitter.X, emitter.Y, emitter.Z};
			const float listeners[] = {listener.X, listener.Y, listener.Z};
			const float extents[] = {half.X, half.Y, half.Z};
			float areaSum = 0, absorptionSum = 0, dampingSum = 0;
			for (int i = 0; i < 6; ++i)
			{
				const int axis = i / 2;
				const float plane = extents[axis] * (i % 2 ? 1.f : -1.f);
				float image[] = {positions[0], positions[1], positions[2]};
				image[axis] = 2 * plane - image[axis];
				const Vec3 mirror{image[0], image[1], image[2]};
				const float fraction = (plane - listeners[axis]) / (image[axis] - listeners[axis]);
				const Vec3 bounce = transform.Position + rotation.Rotate(listener + (mirror - listener) * fraction);
				const Vec3 first = bounce - m_Listener.Position, second = bounce - source;
				const float firstLength = first.Length(), secondLength = second.Length();
				if (firstLength < 0.01f || secondLength < 0.01f) continue;
				const auto firstHits = m_World.GetPhysics().RaycastAll(m_Listener.Position, first, firstLength + 0.03f, ignore, ignoreAlso);
				const RaycastHit* wall = nullptr;
				float transmission = 1, damping = 0;
				for (const auto& hit : firstHits)
				{
					if (std::abs(hit.Distance - firstLength) < 0.06f) { wall = &hit; break; }
					const auto passing = SurfaceSound(m_World, hit.Entity);
					RecordHit(debug, hit, passing, true);
					transmission *= passing.Transmission;
					damping = std::max(damping, passing.Damping);
				}
				if (!wall) continue;
				const auto surface = SurfaceSound(m_World, wall->Entity);
				RecordHit(debug, *wall, surface, true);
				const float area = (axis == 0 ? size.Y * size.Z : axis == 1 ? size.X * size.Z : size.X * size.Y);
				areaSum += area; absorptionSum += area * surface.Absorption; dampingSum += area * surface.Damping;
				for (const auto& hit : m_World.GetPhysics().RaycastAll(source, second, secondLength - 0.03f, ignore, ignoreAlso))
				{
					const auto passing = SurfaceSound(m_World, hit.Entity);
					RecordHit(debug, hit, passing, true);
					transmission *= passing.Transmission;
					damping = std::max(damping, passing.Damping);
				}
				const float length = firstLength + secondLength;
				const auto gains = CalculateAudioSpatialGains(transform.Position + rotation.Rotate(mirror), m_Listener, minDistance, maxDistance);
				auto& reflection = params.Room.Reflections[size_t(i)];
				reflection.Delay = std::clamp((length - distance) / 343.f, 0.001f, 0.5f);
				const float reflectivity = (1 - surface.Absorption) * transmission;
				reflection.LeftGain = gains.Left * reflectivity;
				reflection.RightGain = gains.Right * reflectivity;
				reflection.Damping = std::max(damping, surface.Damping);
				if (debug && reflectivity > 0)
				{
					debug->Rays.push_back({source, bounce, true});
					debug->Rays.push_back({bounce, m_Listener.Position, true});
				}
			}
			if (areaSum > 0)
			{
				const float reflected = 1 - absorptionSum / areaSum;
				params.Room.Decay = std::clamp(room->Decay * reflected, 0.1f, 6.f);
				params.Room.Damping = dampingSum / areaSum;
				params.Room.LateGain *= reflected;
			}
			else params.Room.LateGain = 0;
			return; // first eligible room in creation order wins overlapping volumes
		}
	}

	void AudioWorld::Update()
	{
		if (!m_Playing) return;
		const bool ready = GetDevice() && m_Device->IsReady();
		const bool recovered = ready && m_OutputGeneration != m_Device->GetOutputGeneration();
		if (ready) m_OutputGeneration = m_Device->GetOutputGeneration();
		SelectListener();
		SampleVelocity(m_ListenerEntity, m_Listener.Position);
		auto report = [this](Entity entity, const AudioPlayResult& result) {
			if (!result) FP_LOG_WARN("Audio source '%s': %s", m_World.GetEntityName(entity).CStr(), result.Result.Message.CStr());
		};
		std::vector<std::pair<uint64, Entity>> sources;
		for (Entity entity : m_World.View<AudioSourceComponent>()) sources.emplace_back(m_World.GetCreationIndex(entity), entity);
		std::sort(sources.begin(), sources.end());
		for (const auto& [index, entity] : sources)
		{
			const auto* source = m_World.TryGet<AudioSourceComponent>(entity);
			if (!source) continue;
			const auto found = std::find_if(m_Sources.begin(), m_Sources.end(), [entity](const Source& s) { return s.Owner == entity; });
			const bool added = found == m_Sources.end();
			Source& tracked = added ? TrackSource(entity) : *found;
			const VoiceState state = GetState(tracked.Voice);
			if (state != VoiceState::Invalid) tracked.Recover = source->Loop;
			if (added)
			{
				tracked.RequestedClip = source->Clip;
				tracked.RequestedSpatial = source->Spatial;
				if (!source->Enabled) StopSource(entity);
				else if (source->PlayOnStart)
				{
					tracked.Recover = source->Loop;
					report(entity, Play(entity));
				}
			}
			else if (!source->Enabled) StopSource(entity);
			else if (recovered && tracked.Recover && source->Loop && state == VoiceState::Invalid)
				report(entity, PlayInternal(entity, true));
			else if (state != VoiceState::Invalid && (source->Clip != tracked.RequestedClip || source->Spatial != tracked.RequestedSpatial))
			{
				tracked.RequestedClip = source->Clip;
				tracked.RequestedSpatial = source->Spatial;
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
					voice.Params.Room.Wet = 0;
					if (GetDevice()) m_Device->SetParameters(voice.Handle, voice.Params);
					continue;
				}
				voice.SettingsValid = true;
				voice.Params = params;
				voice.BasePitch = params.Pitch; voice.DopplerScale = source->DopplerScale;
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
