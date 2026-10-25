#pragma once

#include "Engine/Audio/AudioTypes.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace FP
{
	// Per voice so Stop, mute, context cleanup and scene changes also remove its entire tail.
	class AudioRoomEffect
	{
	public:
		explicit AudioRoomEffect(int sampleRate) : m_Rate(sampleRate), m_Early(size_t(sampleRate / 2 + 2), 0)
		{
			constexpr float times[] = {0.0297f, 0.0371f, 0.0411f, 0.0437f};
			for (int i = 0; i < 4; ++i) m_Lines[size_t(i)].assign(size_t(std::round(times[i] * sampleRate)), 0);
		}

		void Configure(const AudioRoomParams& params)
		{
			m_Params = params;
			for (size_t i = 0; i < 4; ++i)
				m_Feedback[i] = std::pow(0.001f, float(m_Lines[i].size()) / (m_Rate * params.Decay));
			m_Tone = float(1 - std::exp(-6.283185307179586 * (500 + 14500 * (1 - params.Damping) * (1 - params.Damping)) / m_Rate));
			for (size_t i = 0; i < m_TapTone.size(); ++i)
			{
				const float cutoff = AudioDampingCutoff(params.Reflections[i].Damping);
				m_TapTone[i] = cutoff == 0 ? 1.f : float(1 - std::exp(-6.283185307179586 * cutoff / m_Rate));
			}
		}

		Vec2 Process(float input)
		{
			m_EarlyTone += m_Tone * (input - m_EarlyTone);
			m_Early[m_Head] = input;
			Vec2 output;
			for (size_t i = 0; i < m_Params.Reflections.size(); ++i)
			{
				const auto& tap = m_Params.Reflections[i];
				const float target = std::clamp(tap.Delay * m_Rate, 1.f, float(m_Early.size() - 2));
				if (!m_Delays[i]) m_Delays[i] = target;
				m_Delays[i] += std::clamp(target - m_Delays[i], -0.02f, 0.02f);
				const size_t delay = size_t(m_Delays[i]);
				const float fraction = m_Delays[i] - float(delay);
				const size_t a = (m_Head + m_Early.size() - delay) % m_Early.size();
				const size_t b = (a + m_Early.size() - 1) % m_Early.size();
				const float sample = m_Early[a] * (1 - fraction) + m_Early[b] * fraction;
				m_TapFiltered[i] = Clean(m_TapFiltered[i] + m_TapTone[i] * (sample - m_TapFiltered[i]));
				output.X += m_TapFiltered[i] * tap.LeftGain;
				output.Y += m_TapFiltered[i] * tap.RightGain;
			}
			m_Head = (m_Head + 1) % m_Early.size();
			float delayed[4];
			for (size_t i = 0; i < 4; ++i)
			{
				const float sample = m_Lines[i][m_Heads[i]];
				m_Damped[i] = Clean(sample * (1 - m_Params.Damping * 0.8f) + m_Damped[i] * m_Params.Damping * 0.8f);
				delayed[i] = m_Damped[i] * m_Feedback[i];
			}
			// Orthogonal feedback keeps the late reverb stable as its decay changes.
			const float feedback[] = {
				(delayed[0] + delayed[1] + delayed[2] + delayed[3]) * 0.5f,
				(delayed[0] - delayed[1] + delayed[2] - delayed[3]) * 0.5f,
				(delayed[0] + delayed[1] - delayed[2] - delayed[3]) * 0.5f,
				(delayed[0] - delayed[1] - delayed[2] + delayed[3]) * 0.5f };
			for (size_t i = 0; i < 4; ++i)
			{
				m_Lines[i][m_Heads[i]] = Clean(m_EarlyTone * 0.25f + feedback[i]);
				m_Heads[i] = (m_Heads[i] + 1) % m_Lines[i].size();
			}
			output.X += (delayed[0] + delayed[2]) * m_Params.LateGain;
			output.Y += (delayed[1] + delayed[3]) * m_Params.LateGain;
			return output;
		}

	private:
		static float Clean(float value) { return std::abs(value) < 1e-20f ? 0.f : value; }
		int m_Rate;
		AudioRoomParams m_Params;
		std::vector<float> m_Early;
		std::array<std::vector<float>, 4> m_Lines;
		size_t m_Head = 0;
		std::array<size_t, 4> m_Heads{};
		std::array<float, 6> m_Delays{};
		std::array<float, 6> m_TapTone{}, m_TapFiltered{};
		std::array<float, 4> m_Damped{}, m_Feedback{};
		float m_EarlyTone = 0, m_Tone = 1;
	};
}
