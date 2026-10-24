#include "Engine/Net/NetworkSystem.h"
#include "Engine/SceneSerializer.h"
#include "HAL/PlatformMisc.h"

#include <cmath>

namespace FP
{
	namespace
	{
		// Leaves room for the message header inside one network message.
		constexpr int32 SpawnChunk = 900;
		// Guid, position, rotation and scale.
		constexpr int32 TransformEntrySize = 16 + 36;
		constexpr int32 TransformsPerMessage = (NetConnection::MaxMessageSize - 16) / TransformEntrySize;
		// Remote entities further than this from where they should be jump instead of gliding.
		constexpr float SnapDistance = 10;
		constexpr float Smoothing = 15;

		void WriteGuid(NetWriter& out, const Guid& id)
		{
			out.WriteU32(id.A);
			out.WriteU32(id.B);
			out.WriteU32(id.C);
			out.WriteU32(id.D);
		}

		Guid ReadGuid(NetReader& in)
		{
			Guid id;
			id.A = in.ReadU32();
			id.B = in.ReadU32();
			id.C = in.ReadU32();
			id.D = in.ReadU32();
			return id;
		}

		// Normalized blend of two rotations, close enough to a slerp for small per frame steps.
		Quat Blend(Quat a, Quat b, float t)
		{
			if (a.X * b.X + a.Y * b.Y + a.Z * b.Z + a.W * b.W < 0)
				b = { -b.X, -b.Y, -b.Z, -b.W };
			Quat q{ a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t, a.Z + (b.Z - a.Z) * t, a.W + (b.W - a.W) * t };
			const float length = std::sqrt(q.X * q.X + q.Y * q.Y + q.Z * q.Z + q.W * q.W);
			return length > 1e-6f ? Quat{ q.X / length, q.Y / length, q.Z / length, q.W / length } : b;
		}
	}

	void NetworkSystem::OnStart(World& world)
	{
		m_SceneEntities.Clear();
		m_Spawned.Clear();
		m_Partial.Clear();
		m_TemplatesReplaced = false;
		for (auto [entity, id, network] : world.View<IdComponent, NetworkComponent>().each())
			m_SceneEntities.Add(id.Id);
	}

	void NetworkSystem::OnStop(World&)
	{
		m_Session.Stop();
	}

	void NetworkSystem::ReplaceTemplates(World& world)
	{
		Array<Entity> templates;
		for (auto [entity, network] : world.View<NetworkComponent>().each())
		{
			if (network.OnePerPeer)
				templates.Add(entity);
		}
		for (Entity entity : templates)
		{
			// Our own copy under a new id. Other peers make theirs and send them over.
			const Guid templateId = world.Get<IdComponent>(entity).Id;
			String text = SceneSerializer::SaveEntityToString(world, entity);
			text.Replace(templateId.ToString().View(), Guid::New().ToString().View());
			m_SceneEntities.Remove(templateId);
			// Destroying waits for the end of the frame, so mark it ours until then or it would be
			// taken for something new our game spawned.
			world.Get<NetworkComponent>(entity).Owner = m_Session.GetLocalId();
			world.DestroyEntity(entity);
			const Entity copy = SceneSerializer::LoadEntityFromString(world, text.View());
			if (copy == NullEntity)
				continue;
			NetworkComponent& network = world.Get<NetworkComponent>(copy);
			network.Owner = m_Session.GetLocalId();
			network.Remote = false;
			m_Spawned.Add(world.Get<IdComponent>(copy).Id);
			SendSpawn(world, copy, 0);
		}
	}

	void NetworkSystem::ClaimEntities(World& world)
	{
		const PeerId local = m_Session.GetLocalId();
		const PeerId authority = m_Session.GetAuthority();
		Array<Entity> spawned;
		for (auto [entity, id, network] : world.View<IdComponent, NetworkComponent>().each())
		{
			if (m_SceneEntities.Contains(id.Id))
			{
				if (network.Owner != authority)
				{
					// Authority moved, so the new owner's ticks start over.
					network.Owner = authority;
					network.LastTick = 0;
					network.HasTarget = false;
				}
			}
			else if (network.Owner == 0)
			{
				// Made by our game while playing, so it is ours to share.
				network.Owner = local;
				spawned.Add(entity);
			}
			network.Remote = network.Owner != local;
		}
		for (Entity entity : spawned)
		{
			m_Spawned.Add(world.Get<IdComponent>(entity).Id);
			SendSpawn(world, entity, 0);
		}

		// Ours and gone, so tell everyone to remove their copies.
		for (int32 i = m_Spawned.Num() - 1; i >= 0; --i)
		{
			if (world.FindEntityByGuid(m_Spawned[i]) != NullEntity)
				continue;
			NetWriter message;
			message.WriteU8(static_cast<uint8>(MessageType::Despawn));
			WriteGuid(message, m_Spawned[i]);
			m_Session.Broadcast(NetChannel::Reliable, message.Data(), message.Size());
			m_Spawned.RemoveAt(i);
		}
	}

	void NetworkSystem::SendSpawn(World& world, Entity entity, PeerId to)
	{
		const String text = SceneSerializer::SaveEntityToString(world, entity);
		const Guid id = world.Get<IdComponent>(entity).Id;
		for (int32 offset = 0; offset < text.Length() || offset == 0; offset += SpawnChunk)
		{
			const int32 length = text.Length() - offset < SpawnChunk ? text.Length() - offset : SpawnChunk;
			NetWriter message;
			message.WriteU8(static_cast<uint8>(MessageType::Spawn));
			WriteGuid(message, id);
			message.WriteU64(m_Session.GetLocalId());
			message.WriteBool(offset + length >= text.Length());
			message.WriteString(text.View().substr(static_cast<usize>(offset), static_cast<usize>(length)));
			if (to == 0)
				m_Session.Broadcast(NetChannel::Reliable, message.Data(), message.Size());
			else
				m_Session.Send(to, NetChannel::Reliable, message.Data(), message.Size());
			if (length == 0)
				break;
		}
	}

	void NetworkSystem::HandleMessage(World& world, const NetEvent& event)
	{
		NetReader reader(event.Data.Data(), event.Data.Num());
		const MessageType type = static_cast<MessageType>(reader.ReadU8());
		switch (type)
		{
			case MessageType::Spawn:
			{
				const Guid id = ReadGuid(reader);
				const PeerId owner = reader.ReadU64();
				const bool last = reader.ReadBool();
				const String chunk = reader.ReadString();
				// Peers only spawn things they own.
				if (!reader.IsValid() || owner != event.Peer)
					return;
				int32 index = m_Partial.FindIf([&](const PartialSpawn& partial) { return partial.From == event.Peer && partial.Id == id; });
				if (index == INDEX_NONE)
				{
					index = m_Partial.Num();
					PartialSpawn& partial = m_Partial.Emplace();
					partial.From = event.Peer;
					partial.Id = id;
				}
				m_Partial[index].Text += chunk.View();
				if (!last)
					return;
				const String text = std::move(m_Partial[index].Text);
				m_Partial.RemoveAt(index);
				if (world.FindEntityByGuid(id) != NullEntity)
					return;
				const Entity entity = SceneSerializer::LoadEntityFromString(world, text.View());
				if (entity == NullEntity)
				{
					FP_LOG_WARN("Could not spawn an entity sent by a peer");
					return;
				}
				NetworkComponent* network = world.TryGet<NetworkComponent>(entity);
				if (!network)
					network = &world.Add<NetworkComponent>(entity);
				network->Owner = owner;
				network->Remote = true;
				break;
			}
			case MessageType::Despawn:
			{
				const Entity entity = world.FindEntityByGuid(ReadGuid(reader));
				const NetworkComponent* network = entity != NullEntity ? world.TryGet<NetworkComponent>(entity) : nullptr;
				if (reader.IsValid() && network && network->Owner == event.Peer)
					world.DestroyEntity(entity);
				break;
			}
			case MessageType::Transforms:
			{
				const uint32 tick = reader.ReadU32();
				const uint16 count = reader.ReadU16();
				for (uint16 i = 0; i < count && reader.IsValid(); ++i)
				{
					const Guid id = ReadGuid(reader);
					const Vec3 position = reader.ReadVec3();
					const Vec3 rotation = reader.ReadVec3();
					const Vec3 scale = reader.ReadVec3();
					const Entity entity = reader.IsValid() ? world.FindEntityByGuid(id) : NullEntity;
					NetworkComponent* network = entity != NullEntity ? world.TryGet<NetworkComponent>(entity) : nullptr;
					// Only the owner moves an entity, and a late update never undoes a newer one.
					if (!network || network->Owner != event.Peer || tick <= network->LastTick || !world.Has<TransformComponent>(entity))
						continue;
					network->LastTick = tick;
					network->TargetPosition = position;
					network->TargetRotation = rotation;
					network->TargetScale = scale;
					if (!network->HasTarget)
					{
						TransformComponent& transform = world.Get<TransformComponent>(entity);
						transform.Position = position;
						transform.Rotation = rotation;
						transform.Scale = scale;
						network->HasTarget = true;
					}
				}
				break;
			}
		}
	}

	void NetworkSystem::SendTransforms(World& world, double now)
	{
		if (m_Session.GetPeers().IsEmpty())
			return;
		NetWriter message;
		int32 count = 0;
		int32 countOffset = 0;
		auto flush = [&]() {
			if (count == 0)
				return;
			message.PatchU16(countOffset, static_cast<uint16>(count));
			m_Session.Broadcast(NetChannel::Unreliable, message.Data(), message.Size());
			count = 0;
		};
		auto begin = [&]() {
			message.Clear();
			message.WriteU8(static_cast<uint8>(MessageType::Transforms));
			message.WriteU32(m_Tick);
			countOffset = message.Size();
			message.WriteU16(0);
		};

		++m_Tick;
		begin();
		for (auto [entity, id, transform, network] : world.View<IdComponent, TransformComponent, NetworkComponent>().each())
		{
			if (network.Remote || now < network.NextSend)
				continue;
			network.NextSend = now + 1.0 / std::fmax(network.SendRate, 1.0f);
			if (count == TransformsPerMessage)
			{
				flush();
				begin();
			}
			WriteGuid(message, id.Id);
			message.WriteVec3(transform.Position);
			message.WriteVec3(transform.Rotation);
			message.WriteVec3(transform.Scale);
			++count;
		}
		flush();
	}

	void NetworkSystem::Smooth(World& world, float deltaTime)
	{
		const float t = 1.0f - std::exp(-Smoothing * deltaTime);
		for (auto [entity, transform, network] : world.View<TransformComponent, NetworkComponent>().each())
		{
			if (!network.Remote || !network.HasTarget)
				continue;
			if ((network.TargetPosition - transform.Position).Length() > SnapDistance)
			{
				transform.Position = network.TargetPosition;
				transform.Rotation = network.TargetRotation;
				transform.Scale = network.TargetScale;
				continue;
			}
			transform.Position += (network.TargetPosition - transform.Position) * t;
			transform.Scale += (network.TargetScale - transform.Scale) * t;
			const Quat blended = Blend(transform.GetRotation(), Quat::FromEuler(network.TargetRotation), t);
			// Land exactly on the target once close, so the angles read the same as the owner's.
			transform.Rotation = t > 0.999f ? network.TargetRotation : blended.ToEuler();
		}
	}

	void NetworkSystem::Update(World& world, float deltaTime)
	{
		if (!m_Session.IsRunning())
			return;
		const double now = PlatformMisc::GetTimeSeconds();
		m_Session.Update(now);

		NetEvent event;
		while (m_Session.PollEvent(event))
		{
			switch (event.Type)
			{
				case NetEventType::PeerJoined:
					// Catch the newcomer up on everything we spawned.
					for (const Guid& id : m_Spawned)
					{
						const Entity entity = world.FindEntityByGuid(id);
						if (entity != NullEntity)
							SendSpawn(world, entity, event.Peer);
					}
					break;
				case NetEventType::PeerLeft:
					for (auto [entity, id, network] : world.View<IdComponent, NetworkComponent>().each())
					{
						if (network.Owner == event.Peer && !m_SceneEntities.Contains(id.Id))
							world.DestroyEntity(entity);
					}
					m_Partial.RemoveAll([&](const PartialSpawn& partial) { return partial.From == event.Peer; });
					break;
				case NetEventType::Message:
					HandleMessage(world, event);
					break;
				case NetEventType::JoinFailed:
					break;
			}
		}

		if (!m_TemplatesReplaced)
		{
			ReplaceTemplates(world);
			m_TemplatesReplaced = true;
		}
		ClaimEntities(world);
		Smooth(world, deltaTime);
		SendTransforms(world, now);
	}
}
