/*
 * LimiterDsp.h - lookahead true-peak brickwall limiter
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#ifndef LMMS_LIMITER_DSP_H
#define LMMS_LIMITER_DSP_H

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace lmms
{

/**
 * Stereo lookahead brickwall limiter.
 *
 * Each channel computes the gain it needs to stay under the ceiling. A sliding minimum holds that gain for the
 * lookahead window, a one-pole lets it recover with the release time, and a moving average over the window turns
 * the step into a smooth ramp that is guaranteed to be fully down when the delayed peak arrives. Optionally the
 * detector looks at a 4x oversampled signal so peaks between samples are caught too (true peak).
 */
class LimiterDsp
{
public:
	static constexpr float MaxLookaheadMs = 10.f;
	static constexpr int TpPhases = 4;
	static constexpr int TpTaps = 16;
	//! The oversampling filter's delay; the plain detector path is delayed by the same amount
	static constexpr int TpDelay = TpTaps / 2;

	LimiterDsp() { designTruePeak(); }

	void setSampleRate(float sampleRate)
	{
		if (sampleRate == m_sampleRate && !m_delay[0].empty()) { return; }
		m_sampleRate = sampleRate;
		const auto capacity = static_cast<std::size_t>(std::ceil(MaxLookaheadMs * 0.001f * sampleRate)) + TpDelay + 2;
		for (auto& d : m_delay) { d.assign(capacity, 0.f); }
		for (auto& ch : m_channels) { ch.allocate(capacity); }
		m_window = 0; // force setLookahead to rebuild
		setLookahead(m_lookaheadMs);
		reset();
	}

	//! Input gain (linear) applied before limiting
	void setInputGain(float gain) { m_inputGain = gain; }
	//! Output ceiling (linear)
	void setCeiling(float ceiling) { m_ceiling = std::max(ceiling, 1e-4f); }
	void setRelease(float ms)
	{
		m_releaseCoeff = 1.f - std::exp(-1.f / (std::max(ms, 0.1f) * 0.001f * m_sampleRate));
	}
	//! 0 = independent channels, 1 = both channels always get the same gain
	void setStereoLink(float link) { m_link = std::clamp(link, 0.f, 1.f); }
	void setTruePeak(bool enabled) { m_truePeak = enabled; }

	void setLookahead(float ms)
	{
		m_lookaheadMs = std::clamp(ms, 0.f, MaxLookaheadMs);
		const int window = std::max(1, static_cast<int>(std::lround(m_lookaheadMs * 0.001f * m_sampleRate)));
		if (window == m_window) { return; }
		m_window = window;
		for (auto& ch : m_channels) { ch.setWindow(window); }
	}

	//! Total delay between input and output in samples
	int latency() const { return m_window - 1 + TpDelay; }

	void reset()
	{
		for (auto& d : m_delay) { std::fill(d.begin(), d.end(), 0.f); }
		for (auto& h : m_tpHistory) { h.fill(0.f); }
		for (auto& ch : m_channels) { ch.reset(); }
		m_write = 0;
	}

	//! Process one stereo frame in place. Returns the applied gain (the smaller of both channels).
	float process(float& left, float& right)
	{
		const float inL = left * m_inputGain;
		const float inR = right * m_inputGain;

		// Detector: sample peak (delayed to line up with the oversampled path) or true peak
		const float peakL = detect(0, inL);
		const float peakR = detect(1, inR);
		float needL = peakL > m_ceiling ? m_ceiling / peakL : 1.f;
		float needR = peakR > m_ceiling ? m_ceiling / peakR : 1.f;
		const float linked = std::min(needL, needR);
		needL += (linked - needL) * m_link;
		needR += (linked - needR) * m_link;

		const float gainL = m_channels[0].process(needL, m_releaseCoeff);
		const float gainR = m_channels[1].process(needR, m_releaseCoeff);

		// Delay the audio so the gain is fully down when the peak comes out
		const auto size = m_delay[0].size();
		m_delay[0][m_write] = inL;
		m_delay[1][m_write] = inR;
		const auto read = (m_write + size - static_cast<std::size_t>(latency())) % size;
		m_write = (m_write + 1) % size;

		// The ramp guarantees the ceiling; the clamp only catches rounding in the running sums
		left = std::clamp(m_delay[0][read] * gainL, -m_ceiling, m_ceiling);
		right = std::clamp(m_delay[1][read] * gainR, -m_ceiling, m_ceiling);
		return std::min(gainL, gainR);
	}

private:
	//! Per channel gain computer: sliding minimum, release, moving average
	class GainChannel
	{
	public:
		void allocate(std::size_t capacity)
		{
			m_minValues.assign(capacity, 1.f);
			m_minIndices.assign(capacity, 0);
			m_box.assign(capacity, 1.f);
		}

		void setWindow(int window)
		{
			m_window = window;
			reset();
		}

		void reset()
		{
			m_head = m_tail = 0;
			m_index = 0;
			m_released = 1.f;
			std::fill(m_box.begin(), m_box.end(), 1.f);
			m_boxSum = m_window;
			m_boxPos = 0;
			m_sinceRefresh = 0;
		}

		float process(float need, float releaseCoeff)
		{
			// Sliding window minimum (monotonic queue)
			const auto cap = m_minValues.size();
			while (m_head != m_tail && m_minValues[(m_tail + cap - 1) % cap] >= need)
			{
				m_tail = (m_tail + cap - 1) % cap;
			}
			m_minValues[m_tail] = need;
			m_minIndices[m_tail] = m_index;
			m_tail = (m_tail + 1) % cap;
			while (m_minIndices[m_head] + m_window <= m_index) { m_head = (m_head + 1) % cap; }
			const float held = m_minValues[m_head];
			++m_index;

			// Instant attack (the ramp comes from the moving average), smooth release
			m_released = held < m_released ? held : m_released + (held - m_released) * releaseCoeff;

			// Moving average over the window
			m_boxSum += m_released - m_box[m_boxPos];
			m_box[m_boxPos] = m_released;
			m_boxPos = (m_boxPos + 1) % static_cast<std::size_t>(m_window);
			if (++m_sinceRefresh >= 4096)
			{
				// Recompute now and then so float rounding cannot pile up
				m_sinceRefresh = 0;
				m_boxSum = 0.0;
				for (int i = 0; i < m_window; ++i) { m_boxSum += m_box[i]; }
			}
			return std::min(1.f, static_cast<float>(m_boxSum / m_window));
		}

	private:
		std::vector<float> m_minValues;
		std::vector<long long> m_minIndices;
		std::size_t m_head = 0;
		std::size_t m_tail = 0;
		long long m_index = 0;
		float m_released = 1.f;
		std::vector<float> m_box;
		double m_boxSum = 1.0;
		std::size_t m_boxPos = 0;
		int m_window = 1;
		int m_sinceRefresh = 0;
	};

	float detect(int channel, float in)
	{
		auto& h = m_tpHistory[channel];
		for (int i = TpTaps - 1; i > 0; --i) { h[i] = h[i - 1]; }
		h[0] = in;
		float peak = std::abs(h[TpDelay]);
		if (m_truePeak)
		{
			for (int phase = 1; phase < TpPhases; ++phase)
			{
				float y = 0.f;
				for (int i = 0; i < TpTaps; ++i) { y += h[i] * m_tpCoeffs[phase][i]; }
				peak = std::max(peak, std::abs(y));
			}
		}
		return peak;
	}

	void designTruePeak()
	{
		// Windowed sinc interpolators, one per fractional position; phase 0 is the plain delayed sample
		for (int phase = 0; phase < TpPhases; ++phase)
		{
			const double frac = static_cast<double>(phase) / TpPhases;
			for (int i = 0; i < TpTaps; ++i)
			{
				const double d = i - TpDelay + frac;
				const double x = std::numbers::pi * d;
				const double sinc = std::abs(d) < 1e-9 ? 1.0 : std::sin(x) / x;
				const double window = 0.5 * (1.0 + std::cos(std::numbers::pi * d / (TpTaps / 2 + 1)));
				m_tpCoeffs[phase][i] = static_cast<float>(sinc * window);
			}
		}
	}

	float m_sampleRate = 0.f;
	float m_inputGain = 1.f;
	float m_ceiling = 1.f;
	float m_releaseCoeff = 0.01f;
	float m_link = 1.f;
	bool m_truePeak = true;
	float m_lookaheadMs = 2.f;
	int m_window = 0;

	std::array<GainChannel, 2> m_channels;
	std::array<std::vector<float>, 2> m_delay;
	std::size_t m_write = 0;
	std::array<std::array<float, TpTaps>, 2> m_tpHistory{};
	std::array<std::array<float, TpTaps>, TpPhases> m_tpCoeffs{};
};

} // namespace lmms

#endif // LMMS_LIMITER_DSP_H
