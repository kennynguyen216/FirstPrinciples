#include "Engine/Net/NetCrypto.h"
#include "HAL/PlatformMisc.h"

#include <monocypher.h>

#include <cstring>

namespace FP::NetCrypto
{
	namespace
	{
		void MakeNonce(uint64 counter, uint8 nonce[24])
		{
			std::memset(nonce, 0, 24);
			std::memcpy(nonce, &counter, 8);
		}

		// The header plus the counter, which the tag covers alongside the ciphertext.
		void MakeAssociated(const uint8* header, int32 headerSize, uint64 counter, Array<uint8>& out)
		{
			out.Resize(headerSize + 8);
			if (headerSize > 0)
				std::memcpy(out.Data(), header, static_cast<usize>(headerSize));
			std::memcpy(out.Data() + headerSize, &counter, 8);
		}
	}

	KeyPair KeyPair::Generate()
	{
		KeyPair keys;
		PlatformMisc::GetRandomBytes(keys.Secret, KeySize);
		crypto_x25519_public_key(keys.Public, keys.Secret);
		return keys;
	}

	void KeyPair::Wipe()
	{
		crypto_wipe(Secret, KeySize);
		crypto_wipe(Public, KeySize);
	}

	void LinkKeys::Wipe()
	{
		crypto_wipe(Send, KeySize);
		crypto_wipe(Receive, KeySize);
	}

	void HashPassword(std::string_view password, uint8 out[KeySize])
	{
		crypto_blake2b(out, KeySize, reinterpret_cast<const uint8*>(password.data()), password.size());
	}

	bool DeriveLinkKeys(const KeyPair& ours, const uint8 theirPublic[KeySize], const uint8 password[KeySize], bool hasPassword, LinkKeys& out)
	{
		uint8 shared[KeySize];
		crypto_x25519(shared, ours.Secret, theirPublic);
		// A broken or hostile public key gives all zeros, which would make the keys guessable.
		uint8 any = 0;
		for (uint8 byte : shared)
			any |= byte;
		if (any == 0)
			return false;

		// Raw shared secrets are not uniformly random, so hash them with both public keys, lower first
		// so both sides hash the same bytes.
		const bool oursLower = std::memcmp(ours.Public, theirPublic, KeySize) < 0;
		uint8 input[KeySize * 3];
		std::memcpy(input, shared, KeySize);
		std::memcpy(input + KeySize, oursLower ? ours.Public : theirPublic, KeySize);
		std::memcpy(input + KeySize * 2, oursLower ? theirPublic : ours.Public, KeySize);
		uint8 material[KeySize * 2];
		crypto_blake2b_keyed(material, sizeof(material), hasPassword ? password : nullptr, hasPassword ? KeySize : 0, input, sizeof(input));

		// The first half seals what the lower key sends, the second what the higher key sends.
		std::memcpy(out.Send, oursLower ? material : material + KeySize, KeySize);
		std::memcpy(out.Receive, oursLower ? material + KeySize : material, KeySize);
		crypto_wipe(shared, sizeof(shared));
		crypto_wipe(input, sizeof(input));
		crypto_wipe(material, sizeof(material));
		return true;
	}

	void Seal(const uint8 key[KeySize], uint64 counter, const uint8* header, int32 headerSize, const uint8* body, int32 bodySize, Array<uint8>& out)
	{
		uint8 nonce[24];
		MakeNonce(counter, nonce);
		Array<uint8> associated;
		MakeAssociated(header, headerSize, counter, associated);

		const int32 start = out.Num();
		out.Resize(start + Overhead + bodySize);
		uint8* counterOut = out.Data() + start;
		uint8* tag = counterOut + 8;
		uint8* cipher = tag + 16;
		std::memcpy(counterOut, &counter, 8);
		crypto_aead_lock(cipher, tag, key, nonce, associated.Data(), static_cast<usize>(associated.Num()), body, static_cast<usize>(bodySize));
	}

	bool Open(const uint8 key[KeySize], const uint8* header, int32 headerSize, const uint8* sealed, int32 sealedSize, uint64& counter, Array<uint8>& out)
	{
		out.Clear();
		if (sealedSize < Overhead)
			return false;
		std::memcpy(&counter, sealed, 8);
		uint8 nonce[24];
		MakeNonce(counter, nonce);
		Array<uint8> associated;
		MakeAssociated(header, headerSize, counter, associated);

		const int32 bodySize = sealedSize - Overhead;
		out.Resize(bodySize);
		if (crypto_aead_unlock(out.Data(), sealed + 8, key, nonce, associated.Data(), static_cast<usize>(associated.Num()), sealed + Overhead, static_cast<usize>(bodySize)) != 0)
		{
			out.Clear();
			return false;
		}
		return true;
	}

	bool ReplayWindow::Accept(uint64 counter)
	{
		if (!m_Any || counter > m_Newest)
		{
			const uint64 shift = m_Any ? counter - m_Newest : 64;
			m_Seen = shift >= 64 ? 0 : m_Seen << shift;
			m_Seen |= 1;
			m_Newest = counter;
			m_Any = true;
			return true;
		}
		const uint64 behind = m_Newest - counter;
		if (behind >= 64 || (m_Seen & (1ull << behind)))
			return false;
		m_Seen |= 1ull << behind;
		return true;
	}
}
