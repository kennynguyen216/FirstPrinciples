#pragma once

#include "HAL/Platform.h"

#include <array>

struct OpusEncoder;
struct OpusDecoder;

namespace FP
{
	inline constexpr uint32 VoiceSampleRate = 48000;
	inline constexpr uint32 VoiceFrameSamples = 960;
	inline constexpr uint32 VoiceMaxPacketBytes = 400;

	class VoiceEncoder
	{
	public:
		VoiceEncoder();
		~VoiceEncoder();
		VoiceEncoder(const VoiceEncoder&) = delete;
		VoiceEncoder& operator=(const VoiceEncoder&) = delete;
		int32 Encode(const float* mono, uint8* out, uint32 capacity);
	private:
		OpusEncoder* m_Encoder = nullptr;
	};

	// Main-thread Opus playout: twelve bounded packets, 60 ms initial jitter allowance.
	class VoiceStream
	{
	public:
		VoiceStream();
		~VoiceStream();
		VoiceStream(const VoiceStream&) = delete;
		VoiceStream& operator=(const VoiceStream&) = delete;
		bool Push(uint32 sequence, uint32 timestamp, const uint8* packet, uint32 size, double now);
		bool Pop(float* mono, double now);
		void Reset();
		bool IsActive(double now) const;
	private:
		struct Packet
		{
			std::array<uint8, VoiceMaxPacketBytes> Bytes{};
			uint32 Sequence = 0, Size = 0;
		};
		void ClearPlayout();
		OpusDecoder* m_Decoder = nullptr;
		std::array<Packet, 12> m_Packets{};
		uint32 m_AnchorSequence = 0, m_AnchorTimestamp = 0, m_Next = 0, m_Highest = 0;
		double m_Due = 0, m_Last = 0;
		bool m_Seen = false, m_Playing = false, m_HasOutput = false;
	};
}
