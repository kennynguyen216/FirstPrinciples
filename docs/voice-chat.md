# Proximity voice chat

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

Voice chat sends microphone speech between networked players and plays received speech at the
corresponding player position. It reuses the network session and spatial audio rather than
introducing a separate communication layer.

## Usage and behavior

For proximity voice, add VoiceChat and Network (`OnePerPeer` enabled) to the player and an
AudioListener to its local listening position. Remote player listeners are ignored. Voice uses the
player's world position, distance settings and the same collider occlusion as audio clips. The
microphone starts only after explicit opt-in. hold V while the game view has focus to transmit. Use
headsets: speaker echo cancellation is not implemented.

The sample `Scenes/ProximityVoice.scene` has two sounds, a networked player and a wall to walk
around. On Windows, run a host and a joiner from the repository root (use the host's LAN address
instead of `127.0.0.1` when playing on another computer):

```powershell
Projects/Sample/Binaries/Debug/Runtime.exe Scenes/ProximityVoice.scene --host 7777 --voice --password example
Projects/Sample/Binaries/Debug/Runtime.exe Scenes/ProximityVoice.scene --join 127.0.0.1:7777 --voice --password example
```

Without `--voice`, the microphone stays off and a player with VoiceChat can still listen. The
editor's Audio... mixer has a separate Voice volume and mute, plus microphone opt-in and per-peer
mute controls when a network system is active. The editor does not create a network session
automatically. the runtime flags above are the first multiplayer test path.

Speech is 48 kHz mono Opus in 20 ms packets over the existing encrypted UDP session. Incoming speech
has a bounded jitter buffer, packet-loss concealment and bounded streaming playback. Packets are
tied to their peer's player, with size and rate limits. Proximity filtering uses peer-replicated
positions. a trusted server/relay is needed to enforce who may receive speech. The existing session
has no relay for peers behind strict NATs.

## How it works

AudioCapture supplies microphone samples after opt-in. VoiceCodec encodes Opus packets, VoiceChat
ties them to session peers, and received audio passes through a bounded playback buffer. Spatial
placement follows replicated player positions and shares the ordinary listener and occlusion rules.

Source: [VoiceChat.h](../src/Engine/Audio/VoiceChat.h),
[VoiceCodec.h](../src/Engine/Audio/VoiceCodec.h),
[AudioCapture.h](../src/Engine/Audio/AudioCapture.h).

## Related guides

[Multiplayer](multiplayer.md), [Audio](audio.md), [Spatial audio](spatial-audio.md).
