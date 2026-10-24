#include "Core/Misc/Guid.h"
#include "HAL/PlatformMisc.h"

namespace FP
{
	Guid Guid::New()
	{
		Guid guid;
		do
		{
			PlatformMisc::GetRandomBytes(&guid, sizeof(guid));
			// Mark as version 4 and RFC 4122 variant.
			guid.B = (guid.B & 0xFFFF0FFFu) | 0x00004000u;
			guid.C = (guid.C & 0x3FFFFFFFu) | 0x80000000u;
		} while (!guid.IsValid());
		return guid;
	}

	String Guid::ToString() const
	{
		return String::Format("%08x-%04x-%04x-%04x-%04x%08x", A, B >> 16, B & 0xFFFF, C >> 16, C & 0xFFFF, D);
	}

	bool Guid::Parse(std::string_view text, Guid& out)
	{
		uint32 words[4] = {};
		int digits = 0;
		for (char c : text)
		{
			if (c == '-' || c == '{' || c == '}')
				continue;
			uint32 value = 0;
			if (c >= '0' && c <= '9')
				value = static_cast<uint32>(c - '0');
			else if (c >= 'a' && c <= 'f')
				value = static_cast<uint32>(c - 'a' + 10);
			else if (c >= 'A' && c <= 'F')
				value = static_cast<uint32>(c - 'A' + 10);
			else
				return false;
			if (digits >= 32)
				return false;
			words[digits / 8] = (words[digits / 8] << 4) | value;
			++digits;
		}
		if (digits != 32)
			return false;
		out = { words[0], words[1], words[2], words[3] };
		return true;
	}
}
