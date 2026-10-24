#pragma once

#include "Core/Hash.h"
#include "Core/String/String.h"

namespace FP
{
	// 128 bit globally unique id. Used to identify objects and assets across saves.
	struct Guid
	{
		uint32 A = 0;
		uint32 B = 0;
		uint32 C = 0;
		uint32 D = 0;

		// Random version 4 guid.
		static Guid New();

		// Accepts "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" with or without dashes and braces.
		static bool Parse(std::string_view text, Guid& out);

		bool IsValid() const { return (A | B | C | D) != 0; }
		void Invalidate() { A = B = C = D = 0; }

		// Lower case 8-4-4-4-12 form.
		String ToString() const;

		auto operator<=>(const Guid&) const = default;
	};

	inline uint32 GetTypeHash(const Guid& guid)
	{
		return HashCombine(HashCombine(MixHash(guid.A), MixHash(guid.B)), HashCombine(MixHash(guid.C), MixHash(guid.D)));
	}
}
