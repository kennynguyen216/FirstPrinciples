#include "Tests/TestFramework.h"
#include "Engine/Audio/AudioLimiter.h"
#include "Engine/Audio/AudioDevice.h"

#include <array>
#include <limits>
#include <vector>

namespace FP { bool AudioMixingOfflineSelfCheck(); }
using namespace FP;

FP_TEST(AudioMixing_TransparentQuietStereoLimitingAndRelease)
{
	AudioLimiter limiter;
	std::array<float, 4> quiet{0.25f, 0.125f, -0.25f, -0.125f};
	const auto original = quiet;
	const auto clean = limiter.Process(quiet.data(), int(quiet.size()), 2, 48000);
	FP_EXPECT(quiet == original && clean.LimitedSamples == 0 && clean.ReductionDb == 0);
	std::array<float, 4> overlap{3.2f, 1.6f, -3.2f, -1.6f};
	const auto limited = limiter.Process(overlap.data(), int(overlap.size()), 2, 48000);
	FP_EXPECT(limited.InputPeak == 3.2f && limited.OutputPeak <= 0.98f && limited.OutputPeak > 0.97f);
	FP_EXPECT(limited.LimitedSamples == 4 && limited.ReductionDb > 10);
	FP_EXPECT(std::abs(overlap[0] - overlap[1] * 2) < 0.00001f && overlap[0] == -overlap[2]);
	std::vector<float> recovery(4800 * 2, 0.2f);
	limiter.Process(recovery.data(), int(recovery.size()), 2, 48000);
	FP_EXPECT(recovery.front() > 0.06f && recovery.back() > recovery.front() && recovery.back() < 0.2f);
	for (int i = 0; i < 12; ++i)
	{
		std::fill(recovery.begin(), recovery.end(), 0.2f);
		limiter.Process(recovery.data(), int(recovery.size()), 2, 48000);
	}
	FP_EXPECT(recovery.back() > 0.199f);
}

FP_TEST(AudioMixing_NonfiniteAndLargeBurstsRemainBounded)
{
	AudioLimiter limiter;
	std::array<float, 4> invalid{std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), 0.1f, -0.1f};
	const auto sanitized = limiter.Process(invalid.data(), int(invalid.size()), 2, 48000);
	FP_EXPECT(sanitized.NonfiniteSamples == 2 && invalid[0] == 0 && invalid[1] == 0);
	std::vector<float> burst(960 * 2, 64 * 0.8f);
	const auto loud = limiter.Process(burst.data(), int(burst.size()), 2, 48000);
	FP_EXPECT(loud.LimitedSamples == burst.size() && loud.OutputPeak <= 0.98f);
	for (float sample : burst) FP_EXPECT(std::isfinite(sample) && sample <= 0.98f && sample > 0.97f);
	FP_EXPECT(limiter.Process(nullptr, 1, 2, 48000).OutputPeak == 0);
}

FP_TEST(AudioMixing_StopFadesInvalidateHandlesAndContextCleanupIsImmediate)
{
	FP_EXPECT(AudioMixingOfflineSelfCheck());
}
