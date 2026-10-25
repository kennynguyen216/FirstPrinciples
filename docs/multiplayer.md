# Peer-to-peer multiplayer

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

Multiplayer synchronizes game entities and messages between peers over UDP. Ownership separates the
peer driving an object from the peers receiving its replicated state.

## Usage and behavior

Games can be peer to peer over UDP. Every peer talks to every other peer directly, with no server.

```bash
Projects/Sample/Binaries/Debug/Runtime --host 7777
Projects/Sample/Binaries/Debug/Runtime --join 127.0.0.1:7777
```

`--host` listens on a port (7777 by default). `--join` connects to any peer already playing. Add
`--password something` on every peer to keep strangers out. That peer introduces the newcomer to
everyone else, so it does not matter whom you join. Try the Sample: start two runtimes, click into
each window and move with WASD.

To share an entity, add a Network component:

- The peer that owns an entity sends its transform `SendRate` times a second. Everyone else glides
  toward it.
- Turn on OnePerPeer for the player. Each peer swaps the scene's copy for its own when the session
  starts, and sees everyone else's.
- Entities your game spawns while playing belong to the peer that spawned them. They show up on every
  peer, go away when destroyed, and leave with their owner.
- Entities from the scene belong to the authority, the peer with the lowest id. When it leaves, the
  next one takes over.
- Game systems should leave other peers' entities alone. Skip any entity whose
  `NetworkComponent::Remote` is true, like `PlayerSystem` in the Sample does.

For your own messages, use the session from the `NetworkSystem`:

```cpp
NetSession& session = world.AddSystem<NetworkSystem>()->GetSession();
session.Start(7777);
session.Broadcast(NetChannel::Reliable, data, size);
```

Messages go on one of three channels:

| Channel | Use for |
|---|---|
| `Reliable` | Events that must arrive: always delivered, exactly once and in order |
| `Sequenced` | State that is replaced often: may be lost, and older ones are dropped |
| `Unreliable` | Anything that can be lost: may arrive out of order |

How it works, bottom to top:

- `UdpSocket` (`src/HAL/PlatformSocket.h`) is a non blocking IPv4 UDP socket.
- `NetConnection` is the reliability layer. Each packet acks the newest one received plus the 32
  before it. Reliable messages are resent when their packet goes unacked for longer than about the
  round trip time, and are put back in order on arrival. It also tracks round trip time and packet
  loss.
- `NetSession` runs the peers: handshakes, introductions, keepalives, timeouts and goodbyes. Both
  sides of a new pair say hello at once, which also gets through most home routers.
- `NetCrypto` encrypts. Each session makes fresh X25519 keys, and every pair of peers swaps public
  keys in its handshake. Each pair derives one key per direction, and every packet after that is
  sealed with XChaCha20-Poly1305 ([Monocypher](https://monocypher.org)). Packets that were changed,
  forged or recorded and played back are dropped. A peer only counts as joined once its packets
  decrypt.
- `NetworkSystem` replicates entities on top.

Limits:

- A message holds about 1000 bytes. Spawns of larger entities are split automatically, but your
  own messages are not.
- Without a password, encryption stops anyone listening in or tampering, but not someone who sits
  in the middle from the very first packet. With `SetPassword` (or `--password`), only peers that
  know the password can connect.
- No cheat protection. Peers that are in a session trust each other.
- The handshake itself is not encrypted, so anyone can see that two addresses are talking, and their
  peer ids and public keys.
- No relay server, so peers behind strict NATs cannot reach each other.
- At most 16 peers.

## How it works

The socket, reliability, session, cryptography and replication layers are separate. NetSession
manages peer connections, NetCrypto seals packets between pairs, and NetworkSystem maps entity
ownership and state onto those sessions. The design has no dedicated authority server or relay
service.

Source: [PlatformSocket.h](../src/HAL/PlatformSocket.h),
[NetConnection.h](../src/Engine/Net/NetConnection.h),
[NetSession.h](../src/Engine/Net/NetSession.h),
[NetworkSystem.h](../src/Engine/Net/NetworkSystem.h).

## Related guides

[Proximity voice](voice-chat.md), [World entities](gameplay.md), [Current limits](limitations.md).
