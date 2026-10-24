#include "Tests/TestFramework.h"
#include "HAL/PlatformSocket.h"

#include <cstring>

using namespace FP;

FP_TEST(Net_ParsesAddresses)
{
	NetAddress address;
	FP_EXPECT(NetAddress::Parse("127.0.0.1:7777", address));
	FP_EXPECT(address == NetAddress::Loopback(7777));
	FP_EXPECT(address.ToString() == "127.0.0.1:7777");
	FP_EXPECT(NetAddress::Parse("10.0.0.5", address, 4000) && address.Port == 4000);
	FP_EXPECT(NetAddress::Parse("localhost:9000", address) && address.Ip == 0x7F000001);
	FP_EXPECT(!NetAddress::Parse("127.0.0.1:99999", address));
	FP_EXPECT(!NetAddress::Parse("127.0.0.1", address));
}

FP_TEST(Net_SocketSendsOverLoopback)
{
	UdpSocket a;
	UdpSocket b;
	FP_EXPECT(a.Open(0) && b.Open(0));
	FP_EXPECT(a.GetPort() != 0 && a.GetPort() != b.GetPort());

	const char message[] = "hello";
	FP_EXPECT(a.Send(NetAddress::Loopback(b.GetPort()), message, sizeof(message)));

	// Loopback delivery is quick but not instant, so poll briefly.
	char buffer[UdpSocket::MaxDatagram];
	NetAddress from;
	int32 received = -1;
	for (int i = 0; i < 1000 && received < 0; ++i)
		received = b.Receive(from, buffer, sizeof(buffer));
	FP_EXPECT(received == static_cast<int32>(sizeof(message)));
	FP_EXPECT(std::memcmp(buffer, message, sizeof(message)) == 0);
	FP_EXPECT(from.Port == a.GetPort());
	FP_EXPECT(b.Receive(from, buffer, sizeof(buffer)) == -1);
}

#include "Engine/Net/NetBuffer.h"

FP_TEST(Net_BufferRoundTrips)
{
	NetWriter writer;
	writer.WriteU8(7);
	writer.WriteU16(65000);
	writer.WriteU32(0xDEADBEEF);
	writer.WriteU64(1ull << 40);
	writer.WriteFloat(1.5f);
	writer.WriteBool(true);
	writer.WriteVec3({ 1, -2, 3 });
	writer.WriteString("peer");

	NetReader reader(writer.Data(), writer.Size());
	FP_EXPECT(reader.ReadU8() == 7);
	FP_EXPECT(reader.ReadU16() == 65000);
	FP_EXPECT(reader.ReadU32() == 0xDEADBEEF);
	FP_EXPECT(reader.ReadU64() == (1ull << 40));
	FP_EXPECT(reader.ReadFloat() == 1.5f);
	FP_EXPECT(reader.ReadBool());
	const Vec3 v = reader.ReadVec3();
	FP_EXPECT(v.X == 1 && v.Y == -2 && v.Z == 3);
	FP_EXPECT(reader.ReadString() == "peer");
	FP_EXPECT(reader.IsValid() && reader.Remaining() == 0);

	// Reading past the end fails safely instead of overrunning.
	FP_EXPECT(reader.ReadU32() == 0);
	FP_EXPECT(!reader.IsValid());
	NetReader truncated(writer.Data(), 2);
	truncated.ReadU8();
	FP_EXPECT(truncated.ReadU16() == 0 && !truncated.IsValid());
}

FP_TEST(Net_SequencesWrapAround)
{
	FP_EXPECT(SequenceGreater(1, 0));
	FP_EXPECT(!SequenceGreater(0, 1));
	FP_EXPECT(SequenceGreater(0, 65535));
	FP_EXPECT(SequenceGreater(5, 65500));
	FP_EXPECT(!SequenceGreater(65500, 5));
	FP_EXPECT(!SequenceGreater(9, 9));
	FP_EXPECT(SequenceLess(65535, 2));
}

#include "Engine/Net/NetConnection.h"

#include <random>

namespace
{
	// A pretend network that drops, duplicates and delays packets so they also arrive out of order.
	struct LossyLink
	{
		struct InFlight
		{
			double ArriveAt;
			Array<uint8> Bytes;
		};

		std::mt19937 Random{ 1234 };
		float Loss = 0;
		float Duplicate = 0;
		double MinDelay = 0;
		double MaxDelay = 0;
		Array<InFlight> Packets;

		void Push(const NetWriter& packet, double now)
		{
			std::uniform_real_distribution<float> chance(0, 1);
			std::uniform_real_distribution<double> delay(MinDelay, MaxDelay);
			const int copies = chance(Random) < Loss ? 0 : (chance(Random) < Duplicate ? 2 : 1);
			for (int i = 0; i < copies; ++i)
			{
				InFlight& flight = Packets.Emplace();
				flight.ArriveAt = now + delay(Random);
				flight.Bytes.Resize(packet.Size());
				std::memcpy(flight.Bytes.Data(), packet.Data(), static_cast<usize>(packet.Size()));
			}
		}

		void Deliver(NetConnection& to, double now)
		{
			for (int32 i = 0; i < Packets.Num();)
			{
				if (Packets[i].ArriveAt <= now)
				{
					NetReader reader(Packets[i].Bytes.Data(), Packets[i].Bytes.Num());
					FP_EXPECT(to.ReadPacket(reader, now));
					Packets.RemoveAtSwap(i);
				}
				else
				{
					++i;
				}
			}
		}
	};

	// Sends every queued packet from one side into the link.
	void Flush(NetConnection& from, LossyLink& link, double now)
	{
		NetWriter packet;
		while (from.WritePacket(packet, now))
			link.Push(packet, now);
	}

	void SendIndex(NetConnection& connection, NetChannel channel, uint32 index, int32 padding = 0)
	{
		NetWriter message;
		message.WriteU32(index);
		for (int32 i = 0; i < padding; ++i)
			message.WriteU8(static_cast<uint8>(index + i));
		FP_EXPECT(connection.Send(channel, message.Data(), message.Size()));
	}

	uint32 ReadIndex(const NetMessage& message)
	{
		NetReader reader(message.Data.Data(), message.Data.Num());
		return reader.ReadU32();
	}
}

FP_TEST(Net_ReliableSurvivesABadLink)
{
	NetConnection a;
	NetConnection b;
	LossyLink aToB;
	LossyLink bToA;
	for (LossyLink* link : { &aToB, &bToA })
	{
		link->Loss = 0.3f;
		link->Duplicate = 0.1f;
		link->MinDelay = 0.02;
		link->MaxDelay = 0.15;
	}
	bToA.Random.seed(99);

	constexpr uint32 Count = 600;
	uint32 sentA = 0;
	uint32 sentB = 0;
	Array<uint32> gotB;
	Array<uint32> gotA;
	uint32 lastSequenced = 0;
	bool sequencedOrdered = true;
	int32 sequencedCount = 0;
	const double step = 1.0 / 60.0;
	double now = 0;
	for (int frame = 0; frame < 60 * 60 && (gotB.Num() < static_cast<int32>(Count) || gotA.Num() < static_cast<int32>(Count)); ++frame)
	{
		now = frame * step;
		// Bursts of reliable traffic both ways, some big, plus state on the sequenced channel.
		for (int i = 0; i < 5 && sentA < Count; ++i, ++sentA)
			SendIndex(a, NetChannel::Reliable, sentA, sentA % 7 == 0 ? 900 : 0);
		for (int i = 0; i < 3 && sentB < Count; ++i, ++sentB)
			SendIndex(b, NetChannel::Reliable, sentB);
		SendIndex(a, NetChannel::Sequenced, static_cast<uint32>(frame));

		Flush(a, aToB, now);
		Flush(b, bToA, now);
		aToB.Deliver(b, now);
		bToA.Deliver(a, now);

		NetMessage message;
		while (b.Receive(message))
		{
			if (message.Channel == NetChannel::Reliable)
			{
				gotB.Add(ReadIndex(message));
			}
			else
			{
				const uint32 value = ReadIndex(message);
				sequencedOrdered = sequencedOrdered && (sequencedCount == 0 || value > lastSequenced);
				lastSequenced = value;
				++sequencedCount;
			}
		}
		while (a.Receive(message))
			gotA.Add(ReadIndex(message));
	}

	// Every reliable message, once each, in order, both ways.
	FP_EXPECT(gotB.Num() == static_cast<int32>(Count));
	FP_EXPECT(gotA.Num() == static_cast<int32>(Count));
	for (int32 i = 0; i < gotB.Num(); ++i)
		FP_EXPECT(gotB[i] == static_cast<uint32>(i));
	for (int32 i = 0; i < gotA.Num(); ++i)
		FP_EXPECT(gotA[i] == static_cast<uint32>(i));

	// Sequenced state arrives newest only, and most of it gets through.
	FP_EXPECT(sequencedOrdered);
	FP_EXPECT(sequencedCount > 0);

	FP_EXPECT(a.ReliableResends > 0);
	FP_EXPECT(a.GetPacketLoss() > 0.1f && a.GetPacketLoss() < 0.6f);
	FP_EXPECT(a.GetRtt() > 0.02f && a.GetRtt() < 0.5f);
	FP_EXPECT(a.GetPendingReliable() == 0);
}

FP_TEST(Net_SequencesAndIdsWrapAround)
{
	// More packets and reliable messages than 16 bits can count, over a perfect link.
	NetConnection a;
	NetConnection b;
	NetWriter packet;
	uint32 expected = 0;
	bool ordered = true;
	for (uint32 i = 0; i < 70000; ++i)
	{
		const double now = i * 0.001;
		SendIndex(a, NetChannel::Reliable, i);
		while (a.WritePacket(packet, now))
		{
			NetReader reader(packet.Data(), packet.Size());
			b.ReadPacket(reader, now);
		}
		while (b.WritePacket(packet, now))
		{
			NetReader reader(packet.Data(), packet.Size());
			a.ReadPacket(reader, now);
		}
		NetMessage message;
		while (b.Receive(message))
			ordered = ordered && ReadIndex(message) == expected++;
	}
	FP_EXPECT(ordered);
	FP_EXPECT(expected == 70000);
	FP_EXPECT(a.PacketsSent > 65536);
	FP_EXPECT(a.ReliableResends == 0);
}

FP_TEST(Net_RejectsBadPackets)
{
	NetConnection connection;
	const uint8 tooShort[] = { 1, 2, 3 };
	NetReader shortReader(tooShort, sizeof(tooShort));
	FP_EXPECT(!connection.ReadPacket(shortReader, 0));

	// A message that claims more bytes than the packet holds.
	NetWriter lying;
	lying.WriteU16(0);
	lying.WriteU16(0);
	lying.WriteU32(0);
	lying.WriteU8(0);
	lying.WriteU8(static_cast<uint8>(NetChannel::Unreliable));
	lying.WriteU16(500);
	lying.WriteU8(1);
	NetReader lyingReader(lying.Data(), lying.Size());
	FP_EXPECT(!connection.ReadPacket(lyingReader, 0));
	NetMessage message;
	FP_EXPECT(!connection.Receive(message));

	Array<uint8> huge;
	huge.Resize(NetConnection::MaxMessageSize + 1);
	FP_EXPECT(!connection.Send(NetChannel::Reliable, huge.Data(), huge.Num()));
}

#include "Engine/Net/NetSession.h"

#include <thread>

namespace
{
	// Runs every session until done says stop, or about two seconds pass.
	template <typename Fn>
	bool Pump(std::initializer_list<NetSession*> sessions, double& now, Fn done)
	{
		for (int i = 0; i < 2000; ++i)
		{
			now += 0.01;
			for (NetSession* session : sessions)
				session->Update(now);
			if (done())
				return true;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return false;
	}

	// Pulls events off a session, keeping only the kind asked for.
	Array<NetEvent> Drain(NetSession& session, NetEventType type)
	{
		Array<NetEvent> events;
		NetEvent event;
		while (session.PollEvent(event))
		{
			if (event.Type == type)
				events.Add(std::move(event));
		}
		return events;
	}
}

FP_TEST(Net_SessionsMeshThroughIntroductions)
{
	NetSession a;
	NetSession b;
	NetSession c;
	FP_EXPECT(a.Start(0) && b.Start(0) && c.Start(0));
	double now = 0;

	// B joins A, then C joins only B. B introduces C and A so everyone ends up connected.
	b.Join(NetAddress::Loopback(a.GetPort()));
	FP_EXPECT(Pump({ &a, &b, &c }, now, [&]() { return a.GetPeers().Num() == 1 && b.GetPeers().Num() == 1; }));
	c.Join(NetAddress::Loopback(b.GetPort()));
	FP_EXPECT(Pump({ &a, &b, &c }, now, [&]() { return a.GetPeers().Num() == 2 && b.GetPeers().Num() == 2 && c.GetPeers().Num() == 2; }));
	FP_EXPECT(a.GetPeers().Contains(c.GetLocalId()));
	FP_EXPECT(c.GetPeers().Contains(a.GetLocalId()));

	// Everyone agrees on who has authority.
	FP_EXPECT(a.GetAuthority() == b.GetAuthority() && b.GetAuthority() == c.GetAuthority());

	Drain(a, NetEventType::Message);
	Drain(b, NetEventType::Message);
	Drain(c, NetEventType::Message);
	const char text[] = "hi all";
	a.Broadcast(NetChannel::Reliable, text, sizeof(text));
	Array<NetEvent> gotB;
	Array<NetEvent> gotC;
	FP_EXPECT(Pump({ &a, &b, &c }, now, [&]() {
		for (NetEvent& event : Drain(b, NetEventType::Message)) gotB.Add(std::move(event));
		for (NetEvent& event : Drain(c, NetEventType::Message)) gotC.Add(std::move(event));
		return gotB.Num() == 1 && gotC.Num() == 1;
	}));
	FP_EXPECT(gotB.Num() == 1 && gotB[0].Peer == a.GetLocalId() && std::memcmp(gotB[0].Data.Data(), text, sizeof(text)) == 0);
	FP_EXPECT(gotC.Num() == 1 && gotC[0].Data.Num() == static_cast<int32>(sizeof(text)));

	// Leaving says goodbye, so the others notice right away instead of after the timeout.
	const PeerId leaving = c.GetLocalId();
	c.Stop();
	FP_EXPECT(Pump({ &a, &b }, now, [&]() { return a.GetPeers().Num() == 1 && b.GetPeers().Num() == 1; }));
	FP_EXPECT(!a.GetPeers().Contains(leaving));
	FP_EXPECT(now < 5.0);
}

FP_TEST(Net_SessionTimesOut)
{
	NetSession a;
	NetSession b;
	FP_EXPECT(a.Start(0) && b.Start(0));
	double now = 0;
	b.Join(NetAddress::Loopback(a.GetPort()));
	FP_EXPECT(Pump({ &a, &b }, now, [&]() { return a.GetPeers().Num() == 1 && b.GetPeers().Num() == 1; }));
	Drain(a, NetEventType::PeerLeft);

	// B goes silent without saying goodbye. A drops it once the timeout passes.
	now += NetSession::Timeout + 1;
	a.Update(now);
	const Array<NetEvent> left = Drain(a, NetEventType::PeerLeft);
	FP_EXPECT(left.Num() == 1 && left[0].Peer == b.GetLocalId());
	FP_EXPECT(a.GetPeers().IsEmpty());

	// Joining a port nobody answers on gives up.
	UdpSocket silent;
	FP_EXPECT(silent.Open(0));
	NetSession c;
	FP_EXPECT(c.Start(0));
	c.Join(NetAddress::Loopback(silent.GetPort()));
	c.Update(100);
	c.Update(111);
	FP_EXPECT(Drain(c, NetEventType::JoinFailed).Num() == 1);
}

#include "Engine/Engine.h"
#include "Engine/Net/NetworkSystem.h"

namespace
{
	int32 CountNamed(World& world, std::string_view name)
	{
		int32 count = 0;
		for (Entity entity : world.GetEntities())
			count += world.GetEntityName(entity) == name ? 1 : 0;
		return count;
	}

	Entity FindNamed(World& world, std::string_view name, bool remote)
	{
		for (auto [entity, network] : world.View<NetworkComponent>().each())
		{
			if (network.Remote == remote && world.GetEntityName(entity) == name)
				return entity;
		}
		return NullEntity;
	}

	bool Near(World& world, const Guid& id, Vec3 position)
	{
		const Entity entity = world.FindEntityByGuid(id);
		return entity != NullEntity && (world.Get<TransformComponent>(entity).Position - position).Length() < 0.01f;
	}
}

FP_TEST(Net_ReplicatesEntitiesBetweenWorlds)
{
	Engine::RegisterEngineComponents();
	const Guid playerId = Guid::New();
	const Guid crateId = Guid::New();
	// Both peers load the same scene: a player template and a shared crate.
	auto makeWorld = [&]() {
		World* world = NewObject<World>();
		world->Add<NetworkComponent>(world->CreateEntity("Player", playerId)).OnePerPeer = true;
		world->Add<NetworkComponent>(world->CreateEntity("Crate", crateId));
		return world;
	};
	StrongObjectPtr<World> worldA(makeWorld());
	StrongObjectPtr<World> worldB(makeWorld());
	World* a = worldA.Get();
	World* b = worldB.Get();
	NetworkSystem* networkA = a->AddSystem<NetworkSystem>();
	NetworkSystem* networkB = b->AddSystem<NetworkSystem>();
	FP_EXPECT(networkA->GetSession().Start(0) && networkB->GetSession().Start(0));
	networkB->GetSession().Join(NetAddress::Loopback(networkA->GetSession().GetPort()));
	a->BeginPlay();
	b->BeginPlay();

	auto pump = [&](auto done, bool withB = true) {
		for (int i = 0; i < 1500; ++i)
		{
			a->Update(1.0f / 60.0f);
			if (withB)
				b->Update(1.0f / 60.0f);
			if (done())
				return true;
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		return false;
	};

	// The template turns into one player per peer, and each peer sees both.
	FP_EXPECT(pump([&]() { return CountNamed(*a, "Player") == 2 && CountNamed(*b, "Player") == 2; }));
	FP_EXPECT(a->FindEntityByGuid(playerId) == NullEntity);
	const Entity mine = FindNamed(*a, "Player", false);
	FP_EXPECT(mine != NullEntity);
	const Guid mineId = a->Get<IdComponent>(mine).Id;
	FP_EXPECT(b->FindEntityByGuid(mineId) != NullEntity && b->Get<NetworkComponent>(b->FindEntityByGuid(mineId)).Remote);

	// Moving our player moves its copy on the other peer.
	a->Get<TransformComponent>(mine).Position = { 5, 1, 0 };
	FP_EXPECT(pump([&]() { return Near(*b, mineId, { 5, 1, 0 }); }));

	// The shared crate follows whichever peer has authority.
	const bool aLeads = networkA->GetSession().GetAuthority() == networkA->GetSession().GetLocalId();
	World& leader = aLeads ? *a : *b;
	World& follower = aLeads ? *b : *a;
	FP_EXPECT(!leader.Get<NetworkComponent>(leader.FindEntityByGuid(crateId)).Remote);
	FP_EXPECT(follower.Get<NetworkComponent>(follower.FindEntityByGuid(crateId)).Remote);
	leader.Get<TransformComponent>(leader.FindEntityByGuid(crateId)).Position = { 0, 3, 0 };
	FP_EXPECT(pump([&]() { return Near(follower, crateId, { 0, 3, 0 }); }));

	// Spawning while playing shows up on the other peer, and so does destroying.
	const Entity bullet = b->CreateEntity("Bullet");
	b->Add<NetworkComponent>(bullet);
	FP_EXPECT(pump([&]() { return CountNamed(*a, "Bullet") == 1; }));
	b->DestroyEntity(bullet);
	FP_EXPECT(pump([&]() { return CountNamed(*a, "Bullet") == 0; }));

	// When B leaves, its player goes with it.
	b->EndPlay();
	FP_EXPECT(pump([&]() { return CountNamed(*a, "Player") == 1; }, false));
	FP_EXPECT(!a->Get<NetworkComponent>(a->FindEntityByGuid(crateId)).Remote);
	a->EndPlay();
}

#include "Engine/Net/NetCrypto.h"

#include <algorithm>

FP_TEST(Net_CryptoSealsAndOpens)
{
	using namespace NetCrypto;
	const KeyPair alice = KeyPair::Generate();
	const KeyPair bob = KeyPair::Generate();
	uint8 noPassword[KeySize] = {};
	LinkKeys aliceKeys;
	LinkKeys bobKeys;
	FP_EXPECT(DeriveLinkKeys(alice, bob.Public, noPassword, false, aliceKeys));
	FP_EXPECT(DeriveLinkKeys(bob, alice.Public, noPassword, false, bobKeys));
	FP_EXPECT(std::memcmp(aliceKeys.Send, bobKeys.Receive, KeySize) == 0);
	FP_EXPECT(std::memcmp(aliceKeys.Receive, bobKeys.Send, KeySize) == 0);
	FP_EXPECT(std::memcmp(aliceKeys.Send, aliceKeys.Receive, KeySize) != 0);

	const uint8 header[] = { 1, 2, 3 };
	const char secret[] = "meet at the crate";
	Array<uint8> sealed;
	Seal(aliceKeys.Send, 7, header, sizeof(header), reinterpret_cast<const uint8*>(secret), sizeof(secret), sealed);
	FP_EXPECT(sealed.Num() == static_cast<int32>(sizeof(secret)) + Overhead);
	// The text never appears as is.
	FP_EXPECT(std::search(sealed.begin(), sealed.end(), secret, secret + 4) == sealed.end());

	uint64 counter = 0;
	Array<uint8> opened;
	FP_EXPECT(Open(bobKeys.Receive, header, sizeof(header), sealed.Data(), sealed.Num(), counter, opened));
	FP_EXPECT(counter == 7 && opened.Num() == static_cast<int32>(sizeof(secret)));
	FP_EXPECT(std::memcmp(opened.Data(), secret, sizeof(secret)) == 0);

	// Any flipped bit, in the body, the counter or the clear header, and it will not open.
	for (int32 i = 0; i < sealed.Num(); ++i)
	{
		Array<uint8> tampered = sealed;
		tampered[i] ^= 0x10;
		FP_EXPECT(!Open(bobKeys.Receive, header, sizeof(header), tampered.Data(), tampered.Num(), counter, opened));
		FP_EXPECT(opened.IsEmpty());
	}
	const uint8 otherHeader[] = { 1, 2, 4 };
	FP_EXPECT(!Open(bobKeys.Receive, otherHeader, sizeof(otherHeader), sealed.Data(), sealed.Num(), counter, opened));
	// Only the right direction's key opens it.
	FP_EXPECT(!Open(bobKeys.Send, header, sizeof(header), sealed.Data(), sealed.Num(), counter, opened));

	// A different password gives keys that do not match.
	uint8 password[KeySize];
	uint8 wrong[KeySize];
	HashPassword("swordfish", password);
	HashPassword("swordfish!", wrong);
	FP_EXPECT(DeriveLinkKeys(alice, bob.Public, password, true, aliceKeys));
	FP_EXPECT(DeriveLinkKeys(bob, alice.Public, wrong, true, bobKeys));
	FP_EXPECT(std::memcmp(aliceKeys.Send, bobKeys.Receive, KeySize) != 0);

	// A zero public key would make the shared secret zero, so it is refused.
	const uint8 zeros[KeySize] = {};
	FP_EXPECT(!DeriveLinkKeys(alice, zeros, noPassword, false, aliceKeys));
}

FP_TEST(Net_ReplayWindowRejectsRepeats)
{
	NetCrypto::ReplayWindow window;
	FP_EXPECT(window.Accept(10));
	FP_EXPECT(!window.Accept(10));
	FP_EXPECT(window.Accept(12));
	FP_EXPECT(window.Accept(11));
	FP_EXPECT(!window.Accept(11));
	FP_EXPECT(window.Accept(200));
	// Too far behind the newest to tell, so refused.
	FP_EXPECT(!window.Accept(100));
	FP_EXPECT(window.Accept(190));
	FP_EXPECT(!window.Accept(190));
}

namespace
{
	// Sits between two peers and forwards everything, like a router on the path. It can read,
	// change and resend packets, which is what encryption has to hold up against.
	struct Middleman
	{
		UdpSocket Socket;
		NetAddress Target;
		NetAddress Client;
		Array<Array<uint8>> Seen;
		int32 Forwarded = 0;
		bool Tamper = false;

		void Pump()
		{
			uint8 buffer[UdpSocket::MaxDatagram];
			NetAddress from;
			int32 size = 0;
			while ((size = Socket.Receive(from, buffer, sizeof(buffer))) >= 0)
			{
				Array<uint8>& copy = Seen.Emplace();
				copy.Resize(size);
				std::memcpy(copy.Data(), buffer, static_cast<usize>(size));
				// Flip a bit in every third data packet past the header.
				if (Tamper && size > 40 && buffer[4] == 2 && ++Forwarded % 3 == 0)
					buffer[size - 1] ^= 1;
				if (from == Target)
					Socket.Send(Client, buffer, static_cast<usize>(size));
				else
				{
					Client = from;
					Socket.Send(Target, buffer, static_cast<usize>(size));
				}
			}
		}

		void ReplayAll()
		{
			for (const Array<uint8>& packet : Seen)
				Socket.Send(Target, packet.Data(), static_cast<usize>(packet.Num()));
		}
	};
}

FP_TEST(Net_SessionEncryptsAndResistsTampering)
{
	NetSession a;
	NetSession b;
	FP_EXPECT(a.Start(0) && b.Start(0));
	Middleman middle;
	FP_EXPECT(middle.Socket.Open(0));
	middle.Target = NetAddress::Loopback(a.GetPort());
	middle.Tamper = true;

	double now = 0;
	b.Join(NetAddress::Loopback(middle.Socket.GetPort()));
	FP_EXPECT(Pump({ &a, &b }, now, [&]() { middle.Pump(); return a.GetPeers().Num() == 1 && b.GetPeers().Num() == 1; }));

	// Reliable messages get through in order even though some packets were changed on the way.
	const char secret[] = "the treasure is under the crate";
	for (uint32 i = 0; i < 40; ++i)
	{
		NetWriter message;
		message.WriteU32(i);
		message.WriteBytes(secret, sizeof(secret));
		b.Broadcast(NetChannel::Reliable, message.Data(), message.Size());
	}
	b.Broadcast(NetChannel::Unreliable, "once", 5);
	Array<uint32> got;
	int32 unreliable = 0;
	FP_EXPECT(Pump({ &a, &b }, now, [&]() {
		middle.Pump();
		for (const NetEvent& event : Drain(a, NetEventType::Message))
		{
			NetReader reader(event.Data.Data(), event.Data.Num());
			if (event.Channel == NetChannel::Reliable)
				got.Add(reader.ReadU32());
			else
				++unreliable;
		}
		return got.Num() == 40;
	}));
	for (int32 i = 0; i < got.Num(); ++i)
		FP_EXPECT(got[i] == static_cast<uint32>(i));
	FP_EXPECT(middle.Forwarded >= 3);

	// Nothing on the wire shows the message.
	bool leaked = false;
	for (const Array<uint8>& packet : middle.Seen)
		leaked = leaked || std::search(packet.begin(), packet.end(), secret, secret + 12) != packet.end();
	FP_EXPECT(!leaked);

	// Played back later, every recorded packet is refused, so nothing arrives twice.
	middle.Tamper = false;
	middle.ReplayAll();
	Pump({ &a, &b }, now, [&]() { middle.Pump(); return false; });
	FP_EXPECT(Drain(a, NetEventType::Message).IsEmpty());
	FP_EXPECT(unreliable <= 1);
	FP_EXPECT(a.GetPeers().Num() == 1);
}

FP_TEST(Net_SessionPasswordsMustMatch)
{
	NetSession a;
	NetSession b;
	NetSession c;
	a.SetPassword("swordfish");
	b.SetPassword("swordfish");
	c.SetPassword("tuna");
	FP_EXPECT(a.Start(0) && b.Start(0) && c.Start(0));
	double now = 0;

	b.Join(NetAddress::Loopback(a.GetPort()));
	FP_EXPECT(Pump({ &a, &b, &c }, now, [&]() { return a.GetPeers().Num() == 1 && b.GetPeers().Num() == 1; }));

	// The wrong password never counts as joined, and the join fails once the timeout passes.
	c.Join(NetAddress::Loopback(a.GetPort()));
	for (int i = 0; i < 50; ++i)
	{
		now += 0.01;
		a.Update(now);
		b.Update(now);
		c.Update(now);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	FP_EXPECT(c.GetPeers().IsEmpty());
	FP_EXPECT(a.GetPeers().Num() == 1);
	FP_EXPECT(Drain(a, NetEventType::PeerJoined).Num() == 1);
	now += NetSession::Timeout + 1;
	c.Update(now);
	FP_EXPECT(Drain(c, NetEventType::JoinFailed).Num() == 1);
}
