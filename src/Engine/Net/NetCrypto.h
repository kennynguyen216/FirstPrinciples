#pragma once

#include "Core/CoreMinimal.h"

namespace FP::NetCrypto
{
	constexpr int32 KeySize = 32;
	// Bytes a sealed packet adds: the 8 byte counter it was sealed with and the 16 byte tag.
	constexpr int32 Overhead = 8 + 16;

	struct KeyPair
	{
		uint8 Public[KeySize] = {};
		uint8 Secret[KeySize] = {};

		// Fresh X25519 keys from the OS random source.
		static KeyPair Generate();
		void Wipe();
	};

	// Keys for one link, one per direction so the two sides never seal with the same key and counter.
	struct LinkKeys
	{
		uint8 Send[KeySize] = {};
		uint8 Receive[KeySize] = {};
		void Wipe();
	};

	// Turns our keys and the other side's public key into link keys. Both sides get matching keys.
	// password is mixed in when set, so peers with a different one end up with keys that do not
	// match. False when their key is unusable.
	bool DeriveLinkKeys(const KeyPair& ours, const uint8 theirPublic[KeySize], const uint8 password[KeySize], bool hasPassword, LinkKeys& out);

	// A password turned into a key. Same text, same key. It needs no slow hashing: an eavesdropper
	// never sees anything to test guesses against, only a peer in the middle of a handshake can.
	void HashPassword(std::string_view password, uint8 out[KeySize]);

	// Encrypts and authenticates body with XChaCha20-Poly1305. header goes out in the clear but is
	// covered by the tag, so it cannot be changed either. Appends counter, tag and ciphertext to out.
	// counter must never repeat for the same key.
	void Seal(const uint8 key[KeySize], uint64 counter, const uint8* header, int32 headerSize, const uint8* body, int32 bodySize, Array<uint8>& out);

	// Undoes Seal on what follows the header. False when anything was changed or the key is wrong,
	// and then out holds nothing.
	bool Open(const uint8 key[KeySize], const uint8* header, int32 headerSize, const uint8* sealed, int32 sealedSize, uint64& counter, Array<uint8>& out);

	// Rejects counters seen before or too far behind the newest one. Stops recorded packets
	// being played back.
	class ReplayWindow
	{
	public:
		// True the first time a counter within the window shows up. Call only after Open succeeded.
		bool Accept(uint64 counter);

	private:
		bool m_Any = false;
		uint64 m_Newest = 0;
		uint64 m_Seen = 0; // bit i is newest - i
	};
}
