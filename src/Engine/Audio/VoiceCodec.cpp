#include "Engine/Audio/VoiceCodec.h"

#include <opus.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace FP
{
	namespace
	{
		constexpr double FrameTime = double(VoiceFrameSamples) / VoiceSampleRate;
		constexpr double SilenceTimeout = .25;
		int32 Delta(uint32 a, uint32 b) { return std::bit_cast<int32>(a - b); }
		bool ValidPacket(const uint8* packet, uint32 size)
		{
			if (!packet || !size || size > VoiceMaxPacketBytes || opus_packet_get_nb_channels(packet) != 1 ||
				opus_packet_get_nb_samples(packet, int(size), VoiceSampleRate) != int(VoiceFrameSamples)) return false;
			unsigned char toc = 0;
			const unsigned char* frames[48]{};
			opus_int16 lengths[48]{};
			int offset = 0;
			return opus_packet_parse(packet, int(size), &toc, frames, lengths, &offset) > 0;
		}
	}

	VoiceEncoder::VoiceEncoder()
	{
		int error = OPUS_OK;
		m_Encoder = opus_encoder_create(VoiceSampleRate, 1, OPUS_APPLICATION_VOIP, &error);
		if (m_Encoder && (error != OPUS_OK || opus_encoder_ctl(m_Encoder, OPUS_SET_BITRATE(24000)) != OPUS_OK))
		{ opus_encoder_destroy(m_Encoder); m_Encoder = nullptr; }
	}
	VoiceEncoder::~VoiceEncoder() { if (m_Encoder) opus_encoder_destroy(m_Encoder); }
	int32 VoiceEncoder::Encode(const float* mono, uint8* out, uint32 capacity)
	{
		if (!m_Encoder || !mono || !out || !capacity) return OPUS_BAD_ARG;
		for (uint32 i = 0; i < VoiceFrameSamples; ++i)
			if (!std::isfinite(mono[i]) || mono[i] < -1 || mono[i] > 1) return OPUS_BAD_ARG;
		return opus_encode_float(m_Encoder, mono, VoiceFrameSamples, out, int(std::min(capacity, VoiceMaxPacketBytes)));
	}

	VoiceStream::VoiceStream()
	{
		int error = OPUS_OK;
		m_Decoder = opus_decoder_create(VoiceSampleRate, 1, &error);
		if (m_Decoder && error != OPUS_OK) { opus_decoder_destroy(m_Decoder); m_Decoder = nullptr; }
	}
	VoiceStream::~VoiceStream() { if (m_Decoder) opus_decoder_destroy(m_Decoder); }
	void VoiceStream::ClearPlayout()
	{
		for (Packet& packet : m_Packets) packet.Size = 0;
		m_Playing = m_HasOutput = false;
		if (m_Decoder) opus_decoder_ctl(m_Decoder, OPUS_RESET_STATE);
	}
	void VoiceStream::Reset()
	{
		ClearPlayout();
		m_Seen = false;
		m_AnchorSequence = m_AnchorTimestamp = m_Next = m_Highest = 0;
		m_Due = m_Last = 0;
	}
	bool VoiceStream::IsActive(double now) const
	{
		return m_Playing && std::isfinite(now) && now >= m_Last && now - m_Last < SilenceTimeout;
	}
	bool VoiceStream::Push(uint32 sequence, uint32 timestamp, const uint8* bytes, uint32 size, double now)
	{
		if (!m_Decoder || !std::isfinite(now) || now < 0 || (m_Seen && now < m_Last) || !ValidPacket(bytes, size)) return false;
		if (m_Seen && timestamp != m_AnchorTimestamp + (sequence - m_AnchorSequence) * VoiceFrameSamples) return false;
		if (m_Playing && !IsActive(now)) ClearPlayout();
		// The capture clock advances during PTT silence; rebuffer its next spurt after a long gap.
		if (m_Playing && Delta(sequence, m_Next) >= int32(m_Packets.size()) && Delta(sequence, m_Highest) > 0 && now - m_Last >= .20)
			ClearPlayout();
		if (!m_Playing)
		{
			// Keep the watermark across silence so a delayed prior spurt cannot restart playback.
			if (m_Seen && Delta(sequence, m_Highest) <= 0) return false;
			if (!m_Seen) { m_AnchorSequence = sequence; m_AnchorTimestamp = timestamp; }
			m_Next = sequence;
			m_Due = now + .06;
			m_Playing = true;
		}
		int32 distance = Delta(sequence, m_Next);
		if (distance < 0 && !m_HasOutput && distance > -int32(m_Packets.size()) && Delta(m_Highest, sequence) < int32(m_Packets.size()))
		{
			m_Next = sequence;
			distance = 0;
		}
		if (distance < 0 || distance >= int32(m_Packets.size())) return false;
		Packet* free = nullptr;
		for (Packet& packet : m_Packets)
		{
			if (packet.Size && packet.Sequence == sequence) return false;
			if (!packet.Size) free = &packet;
		}
		if (!free) return false;
		std::memcpy(free->Bytes.data(), bytes, size);
		free->Sequence = sequence;
		free->Size = size;
		if (!m_Seen || Delta(sequence, m_Highest) > 0) m_Highest = sequence;
		m_Seen = true;
		m_Last = now;
		return true;
	}
	bool VoiceStream::Pop(float* mono, double now)
	{
		if (!mono || !m_Decoder || !std::isfinite(now)) return false;
		if (!IsActive(now)) { if (m_Playing && now >= m_Last) ClearPlayout(); return false; }
		if (now + 1e-9 < m_Due) return false;
		if (now - m_Due > FrameTime * m_Packets.size()) { ClearPlayout(); return false; }
		Packet* next = nullptr;
		for (Packet& packet : m_Packets) if (packet.Size && packet.Sequence == m_Next) { next = &packet; break; }
		int decoded = opus_decode_float(m_Decoder, next ? next->Bytes.data() : nullptr, next ? int(next->Size) : 0, mono, VoiceFrameSamples, 0);
		if (next) next->Size = 0;
		if (decoded < 0) decoded = opus_decode_float(m_Decoder, nullptr, 0, mono, VoiceFrameSamples, 0);
		if (decoded != int(VoiceFrameSamples)) std::fill_n(mono, VoiceFrameSamples, 0.f);
		for (uint32 i = 0; i < VoiceFrameSamples; ++i) mono[i] = std::isfinite(mono[i]) ? std::clamp(mono[i], -1.f, 1.f) : 0;
		++m_Next;
		m_Due += FrameTime;
		m_HasOutput = true;
		return true;
	}
}
