#include "Tests/TestFramework.h"
#include "Engine/Audio/VoiceCodec.h"

#include <array>
#include <cmath>
#include <limits>

using namespace FP;

namespace
{
	struct Encoded
	{
		std::array<uint8, VoiceMaxPacketBytes> Bytes{};
		uint32 Size = 0;
	};
	std::array<Encoded, 16> EncodeTones()
	{
		VoiceEncoder encoder;
		std::array<Encoded, 16> packets;
		std::array<float, VoiceFrameSamples> pcm{};
		for (size_t frame = 0; frame < packets.size(); ++frame)
		{
			for (size_t i = 0; i < pcm.size(); ++i)
				pcm[i] = float(.35 * std::sin((frame * pcm.size() + i) * 6.283185307179586 * 440 / VoiceSampleRate));
			const int32 size = encoder.Encode(pcm.data(), packets[frame].Bytes.data(), VoiceMaxPacketBytes);
			FP_EXPECT(size > 0 && size <= int32(VoiceMaxPacketBytes));
			packets[frame].Size = size > 0 ? uint32(size) : 0;
		}
		return packets;
	}
	bool Push(VoiceStream& stream, uint32 sequence, const Encoded& packet, double now)
	{
		return stream.Push(sequence, sequence * VoiceFrameSamples, packet.Bytes.data(), packet.Size, now);
	}
	void ExpectFinite(const std::array<float, VoiceFrameSamples>& pcm)
	{
		for (float sample : pcm) FP_EXPECT(std::isfinite(sample) && sample >= -1 && sample <= 1);
	}
}

FP_TEST(VoiceCodec_RealOpusFramesAndInputBounds)
{
	const auto packets = EncodeTones();
	VoiceStream stream;
	std::array<float, VoiceFrameSamples> pcm{};
	double energy = 0;
	for (uint32 i = 0; i < 4; ++i) FP_EXPECT(Push(stream, i, packets[i], 1 + i * .005));
	FP_EXPECT(!stream.Pop(pcm.data(), 1.059));
	for (uint32 i = 0; i < 4; ++i)
	{
		FP_EXPECT(stream.Pop(pcm.data(), 1.06 + i * .02));
		ExpectFinite(pcm);
		for (float sample : pcm) energy += sample * sample;
		FP_EXPECT(!stream.Pop(pcm.data(), 1.06 + i * .02));
	}
	FP_EXPECT(energy > .01);
	VoiceEncoder encoder;
	std::array<uint8, VoiceMaxPacketBytes> output{};
	pcm[17] = std::numeric_limits<float>::quiet_NaN();
	FP_EXPECT(encoder.Encode(pcm.data(), output.data(), VoiceMaxPacketBytes) <= 0);
	pcm[17] = 2;
	FP_EXPECT(encoder.Encode(pcm.data(), output.data(), VoiceMaxPacketBytes) <= 0);
	pcm[17] = std::numeric_limits<float>::infinity();
	FP_EXPECT(encoder.Encode(pcm.data(), output.data(), VoiceMaxPacketBytes) <= 0);
	FP_EXPECT(encoder.Encode(nullptr, output.data(), VoiceMaxPacketBytes) <= 0);
	FP_EXPECT(encoder.Encode(pcm.data(), nullptr, VoiceMaxPacketBytes) <= 0);
	FP_EXPECT(encoder.Encode(pcm.data(), output.data(), 0) <= 0);
}

FP_TEST(VoiceCodec_ReordersPacketsAndRejectsLateOrInconsistent)
{
	const auto packets = EncodeTones();
	VoiceStream ordered, reordered;
	for (uint32 i = 0; i < 4; ++i) FP_EXPECT(Push(ordered, 10 + i, packets[i], 1 + i * .005));
	FP_EXPECT(Push(reordered, 12, packets[2], 1));
	FP_EXPECT(Push(reordered, 10, packets[0], 1.005));
	FP_EXPECT(Push(reordered, 13, packets[3], 1.01));
	FP_EXPECT(Push(reordered, 11, packets[1], 1.015));
	FP_EXPECT(!Push(reordered, 12, packets[2], 1.02));
	std::array<float, VoiceFrameSamples> a{}, b{};
	for (uint32 i = 0; i < 4; ++i)
	{
		FP_EXPECT(ordered.Pop(a.data(), 1.06 + i * .02));
		FP_EXPECT(reordered.Pop(b.data(), 1.06 + i * .02));
		for (uint32 sample = 0; sample < VoiceFrameSamples; ++sample) FP_EXPECT(std::abs(a[sample] - b[sample]) < .000001f);
	}
	FP_EXPECT(!Push(reordered, 10, packets[0], 1.13));
	FP_EXPECT(!reordered.Push(14, 14 * VoiceFrameSamples + 1, packets[4].Bytes.data(), packets[4].Size, 1.14));
	FP_EXPECT(!Push(reordered, 14, packets[4], std::numeric_limits<double>::quiet_NaN()));
	FP_EXPECT(!Push(reordered, 14, packets[4], .5));
}

FP_TEST(VoiceCodec_PlcAndBoundedWindow)
{
	const auto packets = EncodeTones();
	VoiceStream stream;
	FP_EXPECT(Push(stream, 0, packets[0], 0));
	FP_EXPECT(Push(stream, 2, packets[2], .01));
	std::array<float, VoiceFrameSamples> pcm{};
	for (int i = 0; i < 3; ++i) { FP_EXPECT(stream.Pop(pcm.data(), .06 + i * .02)); ExpectFinite(pcm); }
	FP_EXPECT(!Push(stream, 1, packets[1], .11));
	stream.Reset();
	for (uint32 i = 0; i < 12; ++i) FP_EXPECT(Push(stream, i, packets[i], 0));
	FP_EXPECT(!Push(stream, 12, packets[12], .01));
	FP_EXPECT(stream.Pop(pcm.data(), .06));
	FP_EXPECT(Push(stream, 12, packets[12], .061));
	FP_EXPECT(!Push(stream, 1000, packets[0], .062));
	FP_EXPECT(!stream.Push(13, 13 * VoiceFrameSamples, packets[0].Bytes.data(), VoiceMaxPacketBytes + 1, .07));
	FP_EXPECT(!stream.Push(13, 13 * VoiceFrameSamples, nullptr, 1, .07));
	const uint8 malformed[] = {0xff};
	FP_EXPECT(!stream.Push(13, 13 * VoiceFrameSamples, malformed, 1, .07));
	const uint8 wrongDuration[] = {0};
	const uint8 stereo[] = {0x0c};
	FP_EXPECT(!stream.Push(13, 13 * VoiceFrameSamples, wrongDuration, 1, .07));
	FP_EXPECT(!stream.Push(13, 13 * VoiceFrameSamples, stereo, 1, .07));
}

FP_TEST(VoiceCodec_WrapSilenceAndFreshSpurts)
{
	const auto packets = EncodeTones();
	VoiceStream stream;
	const uint32 first = std::numeric_limits<uint32>::max() - 1;
	for (uint32 i = 0; i < 4; ++i) FP_EXPECT(Push(stream, first + i, packets[i], i * .005));
	std::array<float, VoiceFrameSamples> pcm{};
	for (uint32 i = 0; i < 4; ++i) { FP_EXPECT(stream.Pop(pcm.data(), .06 + i * .02)); ExpectFinite(pcm); }
	FP_EXPECT(!Push(stream, first, packets[0], .13));
	FP_EXPECT(!stream.IsActive(.266));
	FP_EXPECT(!stream.Pop(pcm.data(), .3));
	FP_EXPECT(!Push(stream, 1, packets[3], .31));
	FP_EXPECT(Push(stream, 30, packets[4], .32));
	FP_EXPECT(stream.IsActive(.33));
	FP_EXPECT(!stream.Pop(pcm.data(), .379));
	FP_EXPECT(stream.Pop(pcm.data(), .38));
	ExpectFinite(pcm);
	// A long forward capture-clock gap starts fresh instead of replaying buffered old speech.
	FP_EXPECT(Push(stream, 80, packets[5], .54));
	FP_EXPECT(!stream.Pop(pcm.data(), .599));
	FP_EXPECT(stream.Pop(pcm.data(), .60));
	stream.Reset();
	FP_EXPECT(Push(stream, 0, packets[0], 2));
	FP_EXPECT(!stream.IsActive(1.9));
	FP_EXPECT(stream.Pop(pcm.data(), 2.06));
}
