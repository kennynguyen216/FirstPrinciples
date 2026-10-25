#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace FP
{
	struct AudioMixLevels
	{
		float InputPeak = 0, OutputPeak = 0, Rms = 0, ReductionDb = 0;
		uint64_t LimitedSamples = 0, NonfiniteSamples = 0;
	};

	// One gain for all channels preserves their balance. No extra buffering or lookahead latency.
	class AudioLimiter
	{
	public:
		AudioMixLevels Process(float* samples, int count, int channels, int sampleRate)
		{
			AudioMixLevels levels;
			if (!samples || count <= 0 || channels <= 0 || sampleRate <= 0) return levels;
			for (int i = 0; i < count; ++i)
			{
				if (!std::isfinite(samples[i])) { samples[i] = 0; ++levels.NonfiniteSamples; }
				levels.InputPeak = std::max(levels.InputPeak, std::abs(samples[i]));
			}
			const float target = levels.InputPeak > 0.98f ? 0.98f / levels.InputPeak : 1.f;
			const float release = float(1 - std::exp(-1.0 / (sampleRate * 0.1)));
			if (m_Gain > target) m_Gain = target;
			double squareSum = 0;
			for (int frame = 0; frame < count; frame += channels)
			{
				m_Gain += (target - m_Gain) * release;
				for (int i = frame; i < std::min(frame + channels, count); ++i)
				{
					samples[i] = std::clamp(samples[i] * m_Gain, -0.98f, 0.98f);
					if (m_Gain < 0.99999f) ++levels.LimitedSamples;
					levels.OutputPeak = std::max(levels.OutputPeak, std::abs(samples[i]));
					squareSum += double(samples[i]) * samples[i];
				}
			}
			levels.Rms = float(std::sqrt(squareSum / count));
			levels.ReductionDb = -20 * std::log10(std::max(m_Gain, 1e-20f));
			return levels;
		}

	private:
		float m_Gain = 1;
	};
}
