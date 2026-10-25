#include "Engine/Audio/VoiceChat.h"
#include "Engine/Audio/AudioCapture.h"
#include "Engine/Audio/AudioDevice.h"
#include "Engine/Audio/AudioWorld.h"
#include "Engine/Audio/VoiceCodec.h"
#include "Engine/Input.h"
#include "Engine/Net/NetSession.h"
#include "Engine/Net/NetworkComponent.h"
#include "Engine/Net/NetworkSystem.h"
#include "HAL/PlatformMisc.h"

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_timer.h>

#include <array>
#include <cmath>
#include <map>

namespace FP
{
	namespace
	{
		bool Valid(const VoiceChatComponent& voice)
		{
			return voice.Enabled && std::isfinite(voice.Volume) && voice.Volume >= 0 && voice.Volume <= 1
				&& std::isfinite(voice.MinDistance) && std::isfinite(voice.MaxDistance)
				&& voice.MinDistance > 0 && voice.MaxDistance > voice.MinDistance;
		}
		Entity Player(World& world, PeerId peer)
		{
			for (Entity entity : world.GetEntities())
			{
				const auto* network = world.TryGet<NetworkComponent>(entity);
				const auto* voice = world.TryGet<VoiceChatComponent>(entity);
				if (network && network->Owner == peer && voice && Valid(*voice) && world.Has<TransformComponent>(entity))
					return entity;
			}
			return NullEntity;
		}
		void WriteGuid(NetWriter& out, const Guid& id)
		{
			out.WriteU32(id.A); out.WriteU32(id.B); out.WriteU32(id.C); out.WriteU32(id.D);
		}
		Guid ReadGuid(NetReader& in)
		{
			Guid id;
			id.A = in.ReadU32(); id.B = in.ReadU32(); id.C = in.ReadU32(); id.D = in.ReadU32();
			return id;
		}
	}

	struct VoiceChat::Impl
	{
		struct Peer
		{
			VoiceStream Stream;
			VoiceHandle Voice;
			Entity Owner = NullEntity;
			bool Muted = false;
			double NextSpatial = 0, RateStart = 0;
			uint32 PacketsThisSecond = 0;
			uint64 QueuedFrames = 0;
		};
		std::map<PeerId, Peer> Peers;
		AudioCapture Capture;
		std::unique_ptr<VoiceEncoder> Encoder;
		AudioDevice* Device = nullptr;
		std::weak_ptr<void> DeviceLifetime;
		uint64 Context = AllocateAudioContext();
		PeerId SessionId = 0;
		uint32 Sequence = 0, Filled = 0, Discarded = 0;
		bool Enabled = false, Talking = false;
		String CaptureStatus = "Microphone off";
		std::array<float, 12000> CaptureSamples{};
		std::array<float, VoiceFrameSamples> Frame{};

		void Stop(Peer& peer)
		{
			if (!DeviceLifetime.expired() && peer.Voice) Device->Stop(peer.Voice);
			peer.Voice = {};
		}
		void Discard(uint32 count)
		{
			Discarded += count;
			Sequence += Discarded / VoiceFrameSamples;
			Discarded %= VoiceFrameSamples;
		}
		void Send(World& world, NetSession& session, Entity local)
		{
			if (!Enabled) return;
			const uint32 count = Capture.Read(CaptureSamples.data(), uint32(CaptureSamples.size()));
			if (!Capture.IsReady()) return;
			const bool down = Input::IsKeyDown(SDL_SCANCODE_V);
			if (down != Talking)
			{
				// The first transmitted sample must have been recorded after push-to-talk was pressed.
				Discard(count + Filled);
				Filled = 0;
				Talking = down;
				return;
			}
			if (!down) { Discard(count); return; }
			uint32 offset = 0;
			if (count > 4 * VoiceFrameSamples)
			{
				offset = count - 4 * VoiceFrameSamples;
				Discard(offset + Filled);
				Filled = 0;
			}
			const auto& settings = world.Get<VoiceChatComponent>(local);
			const Vec3 position = world.GetWorldPosition(local);
			for (; offset < count; ++offset)
			{
				Frame[Filled++] = CaptureSamples[offset];
				if (Filled != VoiceFrameSamples) continue;
				Filled = 0;
				const uint32 sequence = ++Sequence;
				std::array<uint8, VoiceMaxPacketBytes> packet;
				const int32 size = Encoder->Encode(Frame.data(), packet.data(), uint32(packet.size()));
				if (size <= 0) continue;
				NetWriter message;
				message.WriteU8(VoiceChat::MessageType);
				message.WriteU8(1);
				WriteGuid(message, world.Get<IdComponent>(local).Id);
				message.WriteU32(sequence);
				message.WriteU32(sequence * VoiceFrameSamples);
				message.WriteU16(static_cast<uint16>(size));
				message.WriteBytes(packet.data(), size);
				for (PeerId id : session.GetPeers())
				{
					const Entity target = Player(world, id);
					if (target == NullEntity) continue;
					const float distance = (world.GetWorldPosition(target) - position).Length();
					if (std::isfinite(distance) && distance < settings.MaxDistance)
						session.Send(id, NetChannel::Unreliable, message.Data(), message.Size());
				}
			}
		}
	};

	VoiceChat::VoiceChat() : m_Impl(std::make_unique<Impl>()) {}
	VoiceChat::~VoiceChat() { Shutdown(); }

	void VoiceChat::SetEnabled(bool enabled)
	{
		if (enabled == m_Impl->Enabled) return;
		m_Impl->Enabled = enabled;
		m_Impl->Filled = 0;
		m_Impl->Talking = false;
		if (enabled)
		{
			m_Impl->Encoder = std::make_unique<VoiceEncoder>();
			const AudioResult result = m_Impl->Capture.Initialize();
			m_Impl->CaptureStatus = result ? "Microphone ready; hold V to talk" : result.Message;
		}
		else
		{
			m_Impl->Capture.Shutdown();
			m_Impl->Encoder.reset();
			m_Impl->CaptureStatus = "Microphone off; listening";
		}
	}
	bool VoiceChat::IsEnabled() const { return m_Impl->Enabled; }
	void VoiceChat::SetPeerMuted(uint64 id, bool muted)
	{
		if (id == 0 || (!m_Impl->Peers.contains(id) && m_Impl->Peers.size() >= NetSession::MaxPeers)) return;
		auto& peer = m_Impl->Peers[id];
		peer.Muted = muted;
		if (muted) { m_Impl->Stop(peer); peer.Stream.Reset(); }
	}
	bool VoiceChat::IsPeerMuted(uint64 id) const
	{
		const auto found = m_Impl->Peers.find(id);
		return found != m_Impl->Peers.end() && found->second.Muted;
	}
	String VoiceChat::GetStatusText() const
	{
		uint64 frames = 0;
		for (const auto& [id, peer] : m_Impl->Peers) frames += peer.QueuedFrames;
		const String status = m_Impl->Enabled && !m_Impl->Capture.IsReady() ? m_Impl->Capture.GetStatusText() : m_Impl->CaptureStatus;
		return String::Format("%s; received %llu voice frames", status.CStr(), static_cast<unsigned long long>(frames));
	}

	void VoiceChat::Receive(World& world, const NetEvent& event)
	{
		if (event.Type != NetEventType::Message || event.Channel != NetChannel::Unreliable || event.Peer == 0 || event.Data.Num() > NetConnection::MaxMessageSize - 1) return;
		NetReader reader(event.Data.Data(), event.Data.Num());
		if (reader.ReadU8() != MessageType || reader.ReadU8() != 1) return;
		const Guid id = ReadGuid(reader);
		const uint32 sequence = reader.ReadU32(), timestamp = reader.ReadU32();
		const uint16 size = reader.ReadU16();
		if (!reader.IsValid() || size == 0 || size > VoiceMaxPacketBytes || reader.Remaining() != size) return;
		const Entity entity = world.FindEntityByGuid(id);
		if (entity == NullEntity || Player(world, event.Peer) != entity) return;
		if (!m_Impl->Peers.contains(event.Peer) && m_Impl->Peers.size() >= NetSession::MaxPeers) return;
		auto& peer = m_Impl->Peers[event.Peer];
		if (peer.Muted) return;
		const double now = PlatformMisc::GetTimeSeconds();
		if (now - peer.RateStart >= 1) { peer.RateStart = now; peer.PacketsThisSecond = 0; }
		if (++peer.PacketsThisSecond > 75) return;
		if (peer.Owner != entity)
		{
			m_Impl->Stop(peer);
			peer.Stream.Reset();
			peer.Owner = entity;
		}
		peer.Stream.Push(sequence, timestamp, reader.ReadBytes(size), size, now);
	}

	void VoiceChat::Update(World& world, NetSession& session, float)
	{
		auto& impl = *m_Impl;
		if (impl.SessionId && impl.SessionId != session.GetLocalId()) Shutdown();
		impl.SessionId = session.GetLocalId();
		AudioDevice* device = world.GetAudio().GetDevice();
		if (device != impl.Device || impl.DeviceLifetime.expired())
		{
			for (auto& [id, peer] : impl.Peers) impl.Stop(peer);
			impl.Device = device;
			impl.DeviceLifetime = device ? device->GetLifetimeToken() : std::weak_ptr<void>{};
		}
		const Entity local = Player(world, session.GetLocalId());
		if (local == NullEntity)
		{
			for (auto& [id, peer] : impl.Peers) { impl.Stop(peer); peer.Stream.Reset(); }
			if (impl.Capture.IsReady()) impl.Discard(impl.Capture.Read(impl.CaptureSamples.data(), uint32(impl.CaptureSamples.size())));
			impl.Filled = 0;
			impl.Talking = false;
			return;
		}
		impl.Send(world, session, local);
		const double now = PlatformMisc::GetTimeSeconds();
		std::array<float, VoiceFrameSamples> frame;
		for (auto& [id, peer] : impl.Peers)
		{
			const auto* settings = world.IsValid(peer.Owner) ? world.TryGet<VoiceChatComponent>(peer.Owner) : nullptr;
			const bool ownerMatches = Player(world, id) == peer.Owner;
			if (!device || !device->IsReady() || peer.Muted || !settings || !Valid(*settings) || !ownerMatches || !peer.Stream.IsActive(now))
			{
				impl.Stop(peer);
				if (!settings || !Valid(*settings) || !ownerMatches) peer.Stream.Reset();
				continue;
			}
			if (now >= peer.NextSpatial || device->GetState(peer.Voice) == VoiceState::Invalid)
			{
				OneShotParams params;
				params.Owner = peer.Owner;
				params.Spatial = true;
				params.Volume = settings->Volume;
				params.MinDistance = settings->MinDistance;
				params.MaxDistance = settings->MaxDistance;
				params.Occlusion = settings->Occlusion;
				params.Bus = AudioBus::Voice;
				params.Priority = 255;
				const auto spatial = world.GetAudio().GetSpatialParameters(params);
				if (device->GetState(peer.Voice) == VoiceState::Invalid)
					peer.Voice = device->CreateStream(impl.Context, VoiceSampleRate, spatial).Voice;
				else
					device->SetParameters(peer.Voice, spatial);
				peer.NextSpatial = now + .05;
			}
			for (int i = 0; i < 4 && peer.Stream.Pop(frame.data(), now); ++i)
				if (device->QueueStream(peer.Voice, frame.data(), VoiceFrameSamples)) ++peer.QueuedFrames;
		}
	}
	void VoiceChat::RemovePeer(uint64 id)
	{
		const auto peer = m_Impl->Peers.find(id);
		if (peer != m_Impl->Peers.end()) { m_Impl->Stop(peer->second); m_Impl->Peers.erase(peer); }
	}
	void VoiceChat::Shutdown()
	{
		SetEnabled(false);
		for (auto& [id, peer] : m_Impl->Peers) m_Impl->Stop(peer);
		m_Impl->Peers.clear();
		m_Impl->Device = nullptr;
		m_Impl->DeviceLifetime.reset();
		m_Impl->SessionId = 0;
	}

	bool VoiceChatLoopbackSelfCheck()
	{
		const char* driver = SDL_GetHint(SDL_HINT_AUDIO_DRIVER);
		if (!driver || std::string_view(driver) != "dummy") return false;
		AudioDevice device;
		if (!device.Initialize()) return false;
		const Guid playerId = Guid::New();
		auto makeWorld = [&]() {
			World* world = NewObject<World>();
			world->GetAudio().Attach(&device);
			const Entity player = world->CreateEntity("Voice loopback player", playerId);
			world->Add<NetworkComponent>(player).OnePerPeer = true;
			world->Add<VoiceChatComponent>(player);
			world->Add<AudioListenerComponent>(player);
			return world;
		};
		StrongObjectPtr<World> a(makeWorld()), b(makeWorld());
		NetworkSystem* networkA = a->AddSystem<NetworkSystem>();
		NetworkSystem* networkB = b->AddSystem<NetworkSystem>();
		VoiceChat& voiceA = networkA->GetVoiceChat();
		VoiceChat& voiceB = networkB->GetVoiceChat();
		Input::Reset();
		const bool passed = [&]() {
			if (!networkA->GetSession().Start(0) || !networkB->GetSession().Start(0)) return false;
			networkB->GetSession().Join(NetAddress::Loopback(networkA->GetSession().GetPort()));
			a->BeginPlay(); b->BeginPlay();
			auto pump = [&](auto done, double timeout = 1.5) {
				const double until = PlatformMisc::GetTimeSeconds() + timeout;
				do {
					a->Update(.005f); b->Update(.005f); device.Update();
					if (done()) return true;
					SDL_Delay(2);
				} while (PlatformMisc::GetTimeSeconds() < until);
				return false;
			};
			const PeerId idA = networkA->GetSession().GetLocalId(), idB = networkB->GetSession().GetLocalId();
			if (!pump([&]() { return Player(*a.Get(), idB) != NullEntity && Player(*b.Get(), idA) != NullEntity; })) return false;
			if (voiceA.m_Impl->Capture.IsReady() || voiceB.m_Impl->Capture.IsReady()) return false;
			voiceA.SetEnabled(true); voiceB.SetEnabled(true);
			if (!voiceA.m_Impl->Capture.IsReady() || !voiceB.m_Impl->Capture.IsReady()) return false;
			const double unkeyedUntil = PlatformMisc::GetTimeSeconds() + .10;
			if (!pump([&]() { return PlatformMisc::GetTimeSeconds() >= unkeyedUntil; })) return false;
			if (!voiceA.m_Impl->Peers.empty() || !voiceB.m_Impl->Peers.empty()) return false;
			Input::SetKey(SDL_SCANCODE_V, true);
			if (!pump([&]() {
				const auto pa = voiceA.m_Impl->Peers.find(idB), pb = voiceB.m_Impl->Peers.find(idA);
				return pa != voiceA.m_Impl->Peers.end() && pb != voiceB.m_Impl->Peers.end()
					&& pa->second.QueuedFrames >= 3 && pb->second.QueuedFrames >= 3;
			})) return false;
			voiceB.SetPeerMuted(idA, true);
			const uint64 mutedAt = voiceB.m_Impl->Peers.at(idA).QueuedFrames;
			const uint64 activeAt = voiceA.m_Impl->Peers.at(idB).QueuedFrames;
			if (!pump([&]() { return voiceA.m_Impl->Peers.at(idB).QueuedFrames > activeAt + 3; })) return false;
			if (voiceB.m_Impl->Peers.at(idA).Voice || voiceB.m_Impl->Peers.at(idA).QueuedFrames != mutedAt) return false;
			voiceB.SetPeerMuted(idA, false);
			if (!pump([&]() { return voiceB.m_Impl->Peers.at(idA).QueuedFrames > mutedAt + 3; })) return false;
			Input::SetKey(SDL_SCANCODE_V, false);
			if (!pump([&]() { return !voiceA.m_Impl->Peers.at(idB).Voice && !voiceB.m_Impl->Peers.at(idA).Voice; })) return false;
			if (!voiceA.m_Impl->Capture.IsReady() || !voiceB.m_Impl->Capture.IsReady()) return false;
			const uint64 beforeKey = voiceA.m_Impl->Peers.at(idB).QueuedFrames;
			Input::SetKey(SDL_SCANCODE_V, true);
			if (!pump([&]() { return voiceA.m_Impl->Peers.at(idB).QueuedFrames > beforeKey + 2; })) return false;
			a->Get<TransformComponent>(Player(*a.Get(), idA)).Position.X = 1000;
			if (!pump([&]() { return !voiceA.m_Impl->Peers.at(idB).Voice && !voiceB.m_Impl->Peers.at(idA).Voice; })) return false;
			a->Get<TransformComponent>(Player(*a.Get(), idA)).Position.X = 0;
			const uint64 beforeReturn = voiceA.m_Impl->Peers.at(idB).QueuedFrames;
			if (!pump([&]() { return voiceA.m_Impl->Peers.at(idB).Voice && voiceA.m_Impl->Peers.at(idB).QueuedFrames > beforeReturn + 2; })) return false;
			networkA->GetSession().Stop();
			a->Update(.005f);
			if (!a->IsPlaying() || voiceA.IsEnabled() || voiceA.m_Impl->Capture.IsReady() || !voiceA.m_Impl->Peers.empty()) return false;
			Input::SetKey(SDL_SCANCODE_V, false);
			return true;
		}();
		Input::Reset();
		a->EndPlay(); b->EndPlay();
		const bool stopped = !voiceA.m_Impl->Capture.IsReady() && !voiceB.m_Impl->Capture.IsReady()
			&& voiceA.m_Impl->Peers.empty() && voiceB.m_Impl->Peers.empty();
		a->GetAudio().Attach(nullptr); b->GetAudio().Attach(nullptr);
		a.Reset(); b.Reset(); GarbageCollector::Get().Collect();
		return passed && stopped;
	}

	bool VoiceChatOfflineSelfCheck()
	{
		const char* driver = SDL_GetHint(SDL_HINT_AUDIO_DRIVER);
		if (!driver || std::string_view(driver) != "dummy") return false;
		AudioDevice device;
		if (!device.Initialize()) return false;
		StrongObjectPtr<World> world(NewObject<World>());
		world->GetAudio().Attach(&device);
		auto player = [&](PeerId id) {
			const Entity entity = world->CreateEntity("Voice protocol test");
			world->Add<NetworkComponent>(entity).Owner = id;
			world->Add<VoiceChatComponent>(entity);
			return entity;
		};
		const Entity local = player(0);
		world->Add<AudioListenerComponent>(local);
		Entity remote = player(7);
		world->BeginPlay();
		VoiceEncoder encoder;
		std::array<float, VoiceFrameSamples> pcm{};
		for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = float(.2 * std::sin(i * 6.283185307179586 * 440 / VoiceSampleRate));
		std::array<uint8, VoiceMaxPacketBytes> encoded{};
		const int32 bytes = encoder.Encode(pcm.data(), encoded.data(), uint32(encoded.size()));
		if (bytes <= 0) return false;
		auto message = [&](PeerId from, Entity entity, uint32 sequence, uint8 version = 1) {
			NetWriter writer;
			writer.WriteU8(VoiceChat::MessageType); writer.WriteU8(version);
			WriteGuid(writer, world->Get<IdComponent>(entity).Id);
			writer.WriteU32(sequence); writer.WriteU32(sequence * VoiceFrameSamples);
			writer.WriteU16(uint16(bytes)); writer.WriteBytes(encoded.data(), size_t(bytes));
			NetEvent event; event.Peer = from;
			event.Data.Resize(writer.Size());
			for (int32 i = 0; i < writer.Size(); ++i) event.Data[i] = writer.Data()[i];
			return event;
		};
		VoiceChat chat;
		NetSession session;
		auto noCapture = [&]() { return !chat.IsEnabled() && !chat.m_Impl->Capture.IsReady() && !chat.m_Impl->Encoder; };
		if (!noCapture()) return false;
		auto valid = message(7, remote, 100);
		auto bad = valid; bad.Channel = NetChannel::Reliable; chat.Receive(*world.Get(), bad);
		bad = valid; bad.Type = NetEventType::PeerLeft; chat.Receive(*world.Get(), bad);
		bad = message(8, remote, 100); chat.Receive(*world.Get(), bad);
		bad = message(7, local, 100); chat.Receive(*world.Get(), bad);
		bad = message(7, remote, 100, 2); chat.Receive(*world.Get(), bad);
		bad = valid; bad.Data.Add(0); chat.Receive(*world.Get(), bad);
		bad = valid; bad.Data.Resize(10); chat.Receive(*world.Get(), bad);
		bad = valid; bad.Data.Resize(NetConnection::MaxMessageSize); chat.Receive(*world.Get(), bad);
		if (!chat.m_Impl->Peers.empty() || !noCapture()) return false;
		bad = valid; bad.Data.Resize(29); bad.Data[26] = 1; bad.Data[27] = 0; bad.Data[28] = 0xff;
		chat.Receive(*world.Get(), bad);
		if (const auto found = chat.m_Impl->Peers.find(7); found != chat.m_Impl->Peers.end() && found->second.Stream.IsActive(PlatformMisc::GetTimeSeconds())) return false;
		chat.RemovePeer(7);
		chat.Receive(*world.Get(), valid);
		chat.Receive(*world.Get(), valid);
		if (!chat.m_Impl->Peers.contains(7)) return false;
		auto& peer = chat.m_Impl->Peers.at(7);
		const double received = PlatformMisc::GetTimeSeconds();
		if (!peer.Stream.Pop(pcm.data(), received + .061) || peer.Stream.Pop(pcm.data(), received + .061)) return false;
		chat.Receive(*world.Get(), valid);
		if (peer.Stream.Pop(pcm.data(), received + .061)) return false;
		chat.Update(*world.Get(), session, 0);
		const VoiceHandle first = peer.Voice;
		if (!first || device.GetState(first) == VoiceState::Invalid || !noCapture()) return false;
		chat.SetPeerMuted(7, true);
		if (!chat.IsPeerMuted(7) || device.GetState(first) != VoiceState::Invalid || peer.Stream.IsActive(received)) return false;
		chat.Receive(*world.Get(), message(7, remote, 101));
		if (peer.Stream.IsActive(PlatformMisc::GetTimeSeconds())) return false;
		chat.SetPeerMuted(7, false);
		chat.Receive(*world.Get(), message(7, remote, 102));
		chat.Update(*world.Get(), session, 0);
		const VoiceHandle second = peer.Voice;
		if (!second || !noCapture()) return false;
		chat.RemovePeer(7);
		if (chat.m_Impl->Peers.contains(7) || device.GetState(second) != VoiceState::Invalid) return false;
		chat.Receive(*world.Get(), message(7, remote, 200));
		chat.Update(*world.Get(), session, 0);
		const VoiceHandle removed = chat.m_Impl->Peers.at(7).Voice;
		world->DestroyEntity(remote); world->Update(0);
		chat.Update(*world.Get(), session, 0);
		if (device.GetState(removed) != VoiceState::Invalid || chat.m_Impl->Peers.at(7).Stream.IsActive(PlatformMisc::GetTimeSeconds())) return false;
		remote = player(7);
		chat.Receive(*world.Get(), message(7, remote, 300));
		chat.Update(*world.Get(), session, 0);
		const VoiceHandle transferred = chat.m_Impl->Peers.at(7).Voice;
		world->Get<NetworkComponent>(remote).Owner = 8;
		chat.Update(*world.Get(), session, 0);
		if (device.GetState(transferred) != VoiceState::Invalid || chat.m_Impl->Peers.at(7).Stream.IsActive(PlatformMisc::GetTimeSeconds())) return false;
		chat.Shutdown();
		for (int i = 0; i <= NetSession::MaxPeers; ++i)
		{
			const PeerId id = 1000 + i;
			const Entity entity = player(id);
			chat.Receive(*world.Get(), message(id, entity, 1));
		}
		if (chat.m_Impl->Peers.size() != NetSession::MaxPeers || chat.m_Impl->Peers.contains(1000 + NetSession::MaxPeers)) return false;
		chat.SetPeerMuted(9999, true);
		if (chat.m_Impl->Peers.size() != NetSession::MaxPeers) return false;
		chat.Update(*world.Get(), session, 0);
		auto& expires = chat.m_Impl->Peers.at(1000);
		const VoiceHandle expiring = expires.Voice;
		if (!expiring) return false;
		expires.Stream.Pop(pcm.data(), PlatformMisc::GetTimeSeconds() + 1);
		chat.Update(*world.Get(), session, 0);
		if (device.GetState(expiring) != VoiceState::Invalid) return false;
		chat.Shutdown();
		if (!chat.m_Impl->Peers.empty() || chat.m_Impl->Device || !noCapture()) return false;
		world->EndPlay(); world->GetAudio().Attach(nullptr);
		world.Reset(); GarbageCollector::Get().Collect();
		return true;
	}
}
