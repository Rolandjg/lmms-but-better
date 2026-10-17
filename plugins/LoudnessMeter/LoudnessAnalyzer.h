/*
 * LoudnessAnalyzer.h - ITU-R BS.1770 / EBU R128 loudness measurement
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

#ifndef LMMS_LOUDNESS_ANALYZER_H
#define LMMS_LOUDNESS_ANALYZER_H

#include <array>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

namespace lmms
{

//! Measures momentary (400 ms), short-term (3 s) and gated integrated loudness, loudness range and
//! true peak of a stereo signal following ITU-R BS.1770-4 and EBU Tech 3341/3342.
//! Free of allocations after setSampleRate(), so it can run on the audio thread.
class LoudnessAnalyzer
{
public:
	static constexpr float Silence = -std::numeric_limits<float>::infinity();

	void setSampleRate(float sampleRate)
	{
		m_sampleRate = sampleRate;
		designKWeighting(sampleRate);
		m_blockFrames = static_cast<int>(std::lround(sampleRate * 0.1)); // 100 ms hop
		designTruePeak();
		reset();
	}

	void reset()
	{
		// Clear filter state only, the coefficients depend on the sample rate
		for (auto& f : m_filters)
		{
			f.shelf.z1 = f.shelf.z2 = 0.0;
			f.highpass.z1 = f.highpass.z2 = 0.0;
		}
		m_blockSum = 0.0;
		m_blockPos = 0;
		m_subBlocks.fill(0.0);
		m_subBlockCount = 0;
		m_momentaryHist.fill(0);
		m_shortTermHist.fill(0);
		m_momentary = m_shortTerm = m_integrated = Silence;
		m_range = 0.f;
		m_truePeak = 0.f;
		for (auto& h : m_tpHistory) { h.fill(0.f); }
	}

	void process(float left, float right)
	{
		trackTruePeak(left, right);

		const double l = m_filters[0].process(left);
		const double r = m_filters[1].process(right);
		m_blockSum += l * l + r * r;
		if (++m_blockPos >= m_blockFrames) { finishBlock(); }
	}

	float momentary() const { return m_momentary; }
	float shortTerm() const { return m_shortTerm; }
	float integrated() const { return m_integrated; }
	float loudnessRange() const { return m_range; }
	//! Linear true peak since the last reset
	float truePeak() const { return m_truePeak; }
	//! True when a new 100 ms block finished during the last process() call
	bool takeBlockFlag() { const bool f = m_newBlock; m_newBlock = false; return f; }

	static float toLufs(double meanSquare)
	{
		return meanSquare <= 1e-20 ? Silence : static_cast<float>(-0.691 + 10.0 * std::log10(meanSquare));
	}

private:
	struct Biquad
	{
		double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
		double z1 = 0, z2 = 0;
		double process(double x)
		{
			const double y = b0 * x + z1;
			z1 = b1 * x - a1 * y + z2;
			z2 = b2 * x - a2 * y;
			return y;
		}
	};

	struct KFilter
	{
		Biquad shelf;
		Biquad highpass;
		double process(double x) { return highpass.process(shelf.process(x)); }
	};

	// Histogram for gating: 0.1 LU bins from -70 LUFS (the absolute gate) to +5 LUFS
	static constexpr float HistMin = -70.f;
	static constexpr float HistStep = 0.1f;
	static constexpr int HistBins = 751;
	using Histogram = std::array<unsigned, HistBins>;

	void designKWeighting(double rate)
	{
		// Coefficients re-derived for any sample rate, as in libebur128
		{
			const double f0 = 1681.974450955533, gain = 3.999843853973347, q = 0.7071752369554196;
			const double k = std::tan(std::numbers::pi * f0 / rate);
			const double vh = std::pow(10.0, gain / 20.0);
			const double vb = std::pow(vh, 0.4996667741545416);
			const double a0 = 1.0 + k / q + k * k;
			Biquad b;
			b.b0 = (vh + vb * k / q + k * k) / a0;
			b.b1 = 2.0 * (k * k - vh) / a0;
			b.b2 = (vh - vb * k / q + k * k) / a0;
			b.a1 = 2.0 * (k * k - 1.0) / a0;
			b.a2 = (1.0 - k / q + k * k) / a0;
			for (auto& f : m_filters) { f.shelf = b; }
		}
		{
			const double f0 = 38.13547087602444, q = 0.5003270373238773;
			const double k = std::tan(std::numbers::pi * f0 / rate);
			const double a0 = 1.0 + k / q + k * k;
			Biquad b;
			b.b0 = 1.0;
			b.b1 = -2.0;
			b.b2 = 1.0;
			b.a1 = 2.0 * (k * k - 1.0) / a0;
			b.a2 = (1.0 - k / q + k * k) / a0;
			for (auto& f : m_filters) { f.highpass = b; }
		}
	}

	void finishBlock()
	{
		m_subBlocks[m_subBlockCount % m_subBlocks.size()] = m_blockSum / m_blockFrames;
		++m_subBlockCount;
		m_blockSum = 0.0;
		m_blockPos = 0;
		m_newBlock = true;

		const auto meanOf = [this](std::size_t blocks)
		{
			const std::size_t n = std::min<std::size_t>(blocks, m_subBlockCount);
			double sum = 0.0;
			for (std::size_t i = 0; i < n; ++i)
			{
				sum += m_subBlocks[(m_subBlockCount - 1 - i) % m_subBlocks.size()];
			}
			return n ? sum / static_cast<double>(blocks) : 0.0;
		};

		// 400 ms momentary window with 75 % overlap (a block every 100 ms)
		const double momentaryPower = meanOf(4);
		m_momentary = toLufs(momentaryPower);
		if (m_subBlockCount >= 4) { addToHistogram(m_momentaryHist, m_momentary); }
		m_integrated = gatedLoudness(m_momentaryHist, 10.f);

		// 3 s short-term window
		m_shortTerm = toLufs(meanOf(30));
		if (m_subBlockCount >= 30)
		{
			addToHistogram(m_shortTermHist, m_shortTerm);
			m_range = computeRange();
		}
	}

	static void addToHistogram(Histogram& hist, float lufs)
	{
		if (!(lufs >= HistMin)) { return; } // absolute gate, also rejects -inf
		const int bin = std::min(HistBins - 1, static_cast<int>((lufs - HistMin) / HistStep));
		++hist[bin];
	}

	static double binPower(int bin)
	{
		const double lufs = HistMin + (bin + 0.5) * HistStep;
		return std::pow(10.0, (lufs + 0.691) / 10.0);
	}

	//! Mean loudness of the blocks that pass the absolute gate and a relative gate @p relativeGate LU
	//! below the absolute-gated loudness
	static float gatedLoudness(const Histogram& hist, float relativeGate)
	{
		double sum = 0.0;
		unsigned long count = 0;
		for (int i = 0; i < HistBins; ++i)
		{
			if (!hist[i]) { continue; }
			sum += hist[i] * binPower(i);
			count += hist[i];
		}
		if (!count) { return Silence; }
		const float gate = toLufs(sum / count) - relativeGate;
		const int gateBin = std::max(0, static_cast<int>(std::ceil((gate - HistMin) / HistStep)));
		sum = 0.0;
		count = 0;
		for (int i = gateBin; i < HistBins; ++i)
		{
			sum += hist[i] * binPower(i);
			count += hist[i];
		}
		return count ? toLufs(sum / count) : Silence;
	}

	//! EBU Tech 3342: spread between the 10th and 95th percentile of the gated short-term values
	float computeRange() const
	{
		double sum = 0.0;
		unsigned long count = 0;
		for (int i = 0; i < HistBins; ++i)
		{
			sum += m_shortTermHist[i] * binPower(i);
			count += m_shortTermHist[i];
		}
		if (!count) { return 0.f; }
		const float gate = toLufs(sum / count) - 20.f;
		const int gateBin = std::max(0, static_cast<int>(std::ceil((gate - HistMin) / HistStep)));
		unsigned long total = 0;
		for (int i = gateBin; i < HistBins; ++i) { total += m_shortTermHist[i]; }
		if (!total) { return 0.f; }
		const auto percentile = [&](double p)
		{
			const auto target = static_cast<unsigned long>(p * (total - 1));
			unsigned long seen = 0;
			for (int i = gateBin; i < HistBins; ++i)
			{
				seen += m_shortTermHist[i];
				if (seen > target) { return HistMin + (i + 0.5f) * HistStep; }
			}
			return HistMin + (HistBins - 0.5f) * HistStep;
		};
		return percentile(0.95) - percentile(0.10);
	}

	// True peak: 4x oversampling with a windowed-sinc interpolator (EBU Tech 3341 recommends >= 4x)
	static constexpr int TpTaps = 12;
	static constexpr int TpPhases = 4;

	void designTruePeak()
	{
		for (int phase = 0; phase < TpPhases; ++phase)
		{
			const double frac = static_cast<double>(phase) / TpPhases;
			for (int i = 0; i < TpTaps; ++i)
			{
				const double d = -TpTaps / 2 + frac + i;
				const double x = std::numbers::pi * d;
				const double sinc = std::abs(d) < 1e-9 ? 1.0 : std::sin(x) / x;
				const double window = 0.5 * (1.0 + std::cos(std::numbers::pi * d / (TpTaps / 2 + 1)));
				m_tpCoeffs[phase][i] = static_cast<float>(sinc * window);
			}
		}
	}

	void trackTruePeak(float left, float right)
	{
		const std::array<float, 2> in{left, right};
		for (int ch = 0; ch < 2; ++ch)
		{
			auto& h = m_tpHistory[ch];
			for (int i = TpTaps - 1; i > 0; --i) { h[i] = h[i - 1]; }
			h[0] = in[ch];
			float peak = std::abs(in[ch]);
			for (int phase = 1; phase < TpPhases; ++phase)
			{
				float y = 0.f;
				for (int i = 0; i < TpTaps; ++i) { y += h[i] * m_tpCoeffs[phase][i]; }
				peak = std::max(peak, std::abs(y));
			}
			m_truePeak = std::max(m_truePeak, peak);
		}
	}

	float m_sampleRate = 44100.f;
	std::array<KFilter, 2> m_filters;
	int m_blockFrames = 4410;
	int m_blockPos = 0;
	double m_blockSum = 0.0;
	std::array<double, 30> m_subBlocks{};
	std::size_t m_subBlockCount = 0;
	bool m_newBlock = false;

	Histogram m_momentaryHist{};
	Histogram m_shortTermHist{};

	float m_momentary = Silence;
	float m_shortTerm = Silence;
	float m_integrated = Silence;
	float m_range = 0.f;
	float m_truePeak = 0.f;

	std::array<std::array<float, TpTaps>, TpPhases> m_tpCoeffs{};
	std::array<std::array<float, TpTaps>, 2> m_tpHistory{};
};

} // namespace lmms

#endif // LMMS_LOUDNESS_ANALYZER_H
