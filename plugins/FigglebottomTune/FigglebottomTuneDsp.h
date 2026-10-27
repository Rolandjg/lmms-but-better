/*
 * FigglebottomTuneDsp.h - pitch tracking, scale snapping and PSOLA pitch shifting for FigglebottomTune
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

#ifndef LMMS_FIGGLEBOTTOM_TUNE_DSP_H
#define LMMS_FIGGLEBOTTOM_TUNE_DSP_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <vector>

#include "ModernDsp.h"
#include "SampleFrame.h"

namespace lmms::figglebottomtune
{

constexpr int PitchClasses = 12;
using NoteMask = std::array<bool, PitchClasses>;

inline int pitchClass(int note) { return ((note % PitchClasses) + PitchClasses) % PitchClasses; }
inline float frequencyToNote(float hz) { return 69.f + 12.f * std::log2(hz / 440.f); }
inline float noteToFrequency(float note) { return 440.f * std::exp2((note - 69.f) / 12.f); }

inline std::size_t nextPowerOfTwo(std::size_t n)
{
	std::size_t size = 1;
	while (size < n) { size <<= 1; }
	return size;
}


//! Scales as intervals above the root, one bit per semitone
struct Scale
{
	const char* name;
	std::uint16_t intervals;
};

constexpr std::uint16_t intervals(std::initializer_list<int> steps)
{
	std::uint16_t bits = 0;
	for (int step : steps) { bits |= static_cast<std::uint16_t>(1u << step); }
	return bits;
}

//! The last entry, "Custom", means "whatever notes are switched on" and is never applied
constexpr std::array Scales{
	Scale{"Chromatic", intervals({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11})},
	Scale{"Major", intervals({0, 2, 4, 5, 7, 9, 11})},
	Scale{"Minor", intervals({0, 2, 3, 5, 7, 8, 10})},
	Scale{"Harmonic minor", intervals({0, 2, 3, 5, 7, 8, 11})},
	Scale{"Melodic minor", intervals({0, 2, 3, 5, 7, 9, 11})},
	Scale{"Major pentatonic", intervals({0, 2, 4, 7, 9})},
	Scale{"Minor pentatonic", intervals({0, 3, 5, 7, 10})},
	Scale{"Blues", intervals({0, 3, 5, 6, 7, 10})},
	Scale{"Dorian", intervals({0, 2, 3, 5, 7, 9, 10})},
	Scale{"Phrygian", intervals({0, 1, 3, 5, 7, 8, 10})},
	Scale{"Lydian", intervals({0, 2, 4, 6, 7, 9, 11})},
	Scale{"Mixolydian", intervals({0, 2, 4, 5, 7, 9, 10})},
	Scale{"Locrian", intervals({0, 1, 3, 5, 6, 8, 10})},
	Scale{"Whole tone", intervals({0, 2, 4, 6, 8, 10})},
	Scale{"Custom", 0},
};
constexpr int CustomScale = static_cast<int>(Scales.size()) - 1;

inline NoteMask scaleMask(int key, int scale)
{
	NoteMask mask{};
	const auto bits = Scales[std::clamp(scale, 0, CustomScale)].intervals;
	for (int step = 0; step < PitchClasses; ++step)
	{
		mask[pitchClass(key + step)] = (bits >> step) & 1;
	}
	return mask;
}


//! YIN pitch tracker. A coarse search runs on a decimated (~12 kHz) copy of the signal; the
//! winning lag is then refined at the full rate, so the estimate is accurate to a cent or so
//! at a fraction of the cost of a full-rate search.
class PitchDetector
{
public:
	static constexpr float MinFrequency = 60.f;
	static constexpr float MaxFrequency = 1400.f;
	static constexpr int DecimatedHop = 32;
	static constexpr float GateDb = -55.f;

	void setSampleRate(float sampleRate)
	{
		m_sampleRate = sampleRate;
		m_factor = std::max(1, static_cast<int>(std::lround(sampleRate / 12000.f)));
		const float rate = sampleRate / m_factor;
		m_tauMin = std::max(2, static_cast<int>(rate / MaxFrequency));
		m_tauMax = static_cast<int>(std::ceil(rate / MinFrequency)) + 1;
		m_window = m_tauMax;
		m_span = m_window + m_tauMax + 1;
		m_hop = DecimatedHop * m_factor;

		m_decimated.assign(nextPowerOfTwo(m_span * 2), 0.f);
		m_full.assign(nextPowerOfTwo(m_span * m_factor * 2 + 8), 0.f);
		m_frame.assign(m_span, 0.f);
		m_difference.assign(m_tauMax + 2, 0.f);

		m_highpass.set(dsp::Biquad::Type::Highpass, 50.f, 0.707f, sampleRate);
		// 4th order Butterworth against aliasing in the decimated copy
		const float cutoff = std::min(rate * 0.4f, sampleRate * 0.45f);
		m_lowpass[0].set(dsp::Biquad::Type::Lowpass, cutoff, 0.5412f, sampleRate);
		m_lowpass[1].set(dsp::Biquad::Type::Lowpass, cutoff, 1.3066f, sampleRate);
		reset();
	}

	void reset()
	{
		std::fill(m_decimated.begin(), m_decimated.end(), 0.f);
		std::fill(m_full.begin(), m_full.end(), 0.f);
		m_highpass.reset();
		for (auto& filter : m_lowpass) { filter.reset(); }
		m_writeDecimated = m_writeFull = 0;
		m_phase = m_hopCounter = 0;
		m_frequency = 0.f;
		m_level = -120.f;
	}

	//! YIN threshold: lower is stricter (fewer, cleaner detections)
	void setThreshold(float threshold) { m_threshold = threshold; }

	//! Full-rate samples between estimates
	int hop() const { return m_hop; }
	//! Full-rate samples one estimate looks back over; it describes the middle of that stretch
	int span() const { return m_span * m_factor; }

	//! Feed one mono sample. Returns true when a new estimate is ready.
	bool process(float x)
	{
		const float h = m_highpass.process(x);
		m_full[m_writeFull++ & (m_full.size() - 1)] = h;
		const float l = m_lowpass[1].process(m_lowpass[0].process(h));
		if (++m_phase < m_factor) { return false; }
		m_phase = 0;
		m_decimated[m_writeDecimated++ & (m_decimated.size() - 1)] = l;
		if (++m_hopCounter < DecimatedHop) { return false; }
		m_hopCounter = 0;
		analyse();
		return true;
	}

	//! Detected fundamental in Hz, or 0 when there is no clear pitch
	float frequency() const { return m_frequency; }
	//! RMS of the analysed stretch in dBFS
	float level() const { return m_level; }

private:
	void analyse()
	{
		const std::size_t mask = m_decimated.size() - 1;
		const std::size_t start = m_writeDecimated - m_span;
		double energy = 0.0;
		for (int i = 0; i < m_span; ++i)
		{
			m_frame[i] = m_decimated[(start + i) & mask];
			energy += m_frame[i] * m_frame[i];
		}
		m_level = 10.f * std::log10(std::max(static_cast<float>(energy / m_span), 1e-12f));
		m_frequency = 0.f;
		if (m_level < GateDb) { return; }

		// Cumulative mean normalised difference
		m_difference[0] = 1.f;
		double running = 0.0;
		for (int tau = 1; tau <= m_tauMax; ++tau)
		{
			float sum = 0.f;
			for (int j = 0; j < m_window; ++j)
			{
				const float d = m_frame[j] - m_frame[j + tau];
				sum += d * d;
			}
			running += sum;
			m_difference[tau] = running > 0.0 ? static_cast<float>(sum * tau / running) : 1.f;
		}

		// The first dip under the threshold, followed down to its bottom
		int tau = -1;
		for (int t = m_tauMin; t < m_tauMax; ++t)
		{
			if (m_difference[t] < m_threshold)
			{
				while (t + 1 < m_tauMax && m_difference[t + 1] < m_difference[t]) { ++t; }
				tau = t;
				break;
			}
		}
		if (tau < 0) { return; }

		float coarse = static_cast<float>(tau) + parabolicOffset(m_difference[tau - 1], m_difference[tau],
			m_difference[tau + 1]);
		m_frequency = m_sampleRate / (coarse * m_factor);
		if (m_factor > 1) { refine(coarse * m_factor); }
	}

	//! Plain difference function at the full rate, around the coarse lag
	void refine(float lag)
	{
		const std::size_t mask = m_full.size() - 1;
		const int window = m_window * m_factor;
		const std::size_t start = m_writeFull - static_cast<std::size_t>(span());
		const int lo = std::max(2, static_cast<int>(lag) - m_factor - 1);
		const int hi = std::min(m_tauMax * m_factor - 1, static_cast<int>(lag) + m_factor + 2);
		if (hi - lo < 2) { return; }

		std::array<float, 64> d{};
		const int count = std::min(hi - lo + 1, static_cast<int>(d.size()));
		for (int k = 0; k < count; ++k)
		{
			const auto tau = static_cast<std::size_t>(lo + k);
			float sum = 0.f;
			for (int j = 0; j < window; ++j)
			{
				const float diff = m_full[(start + j) & mask] - m_full[(start + j + tau) & mask];
				sum += diff * diff;
			}
			d[k] = sum;
		}
		const int best = static_cast<int>(std::min_element(d.begin() + 1, d.begin() + count - 1) - d.begin());
		const float fine = static_cast<float>(lo + best) + parabolicOffset(d[best - 1], d[best], d[best + 1]);
		m_frequency = m_sampleRate / fine;
	}

	static float parabolicOffset(float a, float b, float c)
	{
		const float denom = a - 2.f * b + c;
		return std::abs(denom) < 1e-12f ? 0.f : std::clamp(0.5f * (a - c) / denom, -0.5f, 0.5f);
	}

	float m_sampleRate = 44100.f;
	int m_factor = 1;
	int m_tauMin = 2;
	int m_tauMax = 2;
	int m_window = 1;
	int m_span = 1;
	int m_hop = DecimatedHop;
	float m_threshold = 0.2f;

	dsp::Biquad m_highpass;
	std::array<dsp::Biquad, 2> m_lowpass;
	std::vector<float> m_decimated;
	std::vector<float> m_full;
	std::vector<float> m_frame;
	std::vector<float> m_difference;
	std::size_t m_writeDecimated = 0;
	std::size_t m_writeFull = 0;
	int m_phase = 0;
	int m_hopCounter = 0;

	float m_frequency = 0.f;
	float m_level = -120.f;
};


//! How the detected pitch is pulled onto the scale
struct CorrectorSettings
{
	NoteMask notes{};
	float retuneSeconds = 0.f;  //!< time constant of the pull towards the target
	float glideSeconds = 0.f;   //!< time constant of the jump between target notes
	float amount = 1.f;         //!< 0..1 share of the correction applied
	float humanize = 0.f;       //!< 0..1 share of vibrato kept through the correction
	float sticky = 0.f;         //!< semitones the pitch must pass the midpoint by to switch note
	float tolerance = 0.f;      //!< semitones around the target left untouched
	float shift = 0.f;          //!< transposition of the corrected pitch in semitones
};

struct Correction
{
	bool voiced = false;
	float input = 0.f;  //!< sung pitch, MIDI note number
	float output = 0.f; //!< corrected pitch before the shift
	int target = -1;    //!< the scale note being snapped to, -1 when none
	float ratio = 1.f;  //!< pitch shift factor to apply
	float period = 0.f; //!< input period in samples
};

//! Hop-rate state machine from detected frequency to pitch shift ratio
class Corrector
{
public:
	void setTiming(float hopSeconds, float sampleRate)
	{
		m_hopSeconds = hopSeconds;
		m_sampleRate = sampleRate;
		// Vibrato is what a ~2 Hz high-pass lets through
		m_slowCoeff = coefficient(0.08f);
	}

	void reset()
	{
		m_voiced = false;
		m_missed = 0;
		m_target = -1;
	}

	Correction process(float frequency, const CorrectorSettings& s)
	{
		// Bridge single dropouts in the middle of a note
		if (frequency <= 0.f && m_voiced && m_missed < 3)
		{
			frequency = m_lastFrequency;
			++m_missed;
		}
		else if (frequency > 0.f) { m_missed = 0; }

		Correction c;
		if (frequency <= 0.f)
		{
			m_voiced = false;
			m_target = -1;
			return c;
		}
		m_lastFrequency = frequency;
		const float raw = frequencyToNote(frequency);

		const bool onset = !m_voiced;
		m_voiced = true;
		if (onset)
		{
			m_recent.fill(raw);
			m_slow = raw;
			m_correction = 0.f;
		}
		// Median of three throws out single-hop octave errors
		m_recent = {m_recent[1], m_recent[2], raw};
		auto sorted = m_recent;
		std::sort(sorted.begin(), sorted.end());
		const float note = sorted[1];

		m_slow += (note - m_slow) * m_slowCoeff;
		const float vibrato = note - m_slow;
		// Pick the note from the pitch without its vibrato, so a wide vibrato doesn't flip notes
		const float steady = note - s.humanize * vibrato;

		const int nearest = nearestNote(steady, s.notes);
		if (nearest < 0)
		{
			m_target = -1;
			c.voiced = true;
			c.input = c.output = note;
			c.ratio = std::exp2(s.shift / 12.f);
			c.period = m_sampleRate / noteToFrequency(note);
			return c;
		}
		const bool keep = m_target >= 0 && s.notes[pitchClass(m_target)]
			&& std::abs(steady - m_target) <= std::abs(steady - nearest) + s.sticky;
		if (!keep) { m_target = nearest; }
		if (onset) { m_glide = static_cast<float>(m_target); }
		m_glide += (m_target - m_glide) * coefficient(s.glideSeconds);

		float wanted = m_glide + s.humanize * vibrato - note;
		wanted = std::copysign(std::max(0.f, std::abs(wanted) - s.tolerance), wanted);
		m_correction += (wanted - m_correction) * coefficient(s.retuneSeconds);

		c.voiced = true;
		c.input = note;
		c.output = note + s.amount * m_correction;
		c.target = m_target;
		c.ratio = std::clamp(std::exp2((c.output + s.shift - note) / 12.f), 0.4f, 2.5f);
		c.period = m_sampleRate / noteToFrequency(note);
		return c;
	}

private:
	float coefficient(float seconds) const
	{
		return seconds <= 0.f ? 1.f : 1.f - std::exp(-m_hopSeconds / seconds);
	}

	static int nearestNote(float note, const NoteMask& notes)
	{
		const int base = static_cast<int>(std::floor(note));
		int best = -1;
		float bestDistance = std::numeric_limits<float>::max();
		for (int candidate = base - 6; candidate <= base + 7; ++candidate)
		{
			const float distance = std::abs(note - candidate);
			if (notes[pitchClass(candidate)] && distance < bestDistance)
			{
				best = candidate;
				bestDistance = distance;
			}
		}
		return best;
	}

	float m_hopSeconds = 0.003f;
	float m_sampleRate = 44100.f;
	float m_slowCoeff = 0.f;

	bool m_voiced = false;
	int m_missed = 0;
	float m_lastFrequency = 0.f;
	std::array<float, 3> m_recent{};
	float m_slow = 0.f;
	int m_target = -1;
	float m_glide = 0.f;
	float m_correction = 0.f;
};


//! Pitch-synchronous overlap-add shifter: grains spanning a period either side of an input pitch
//! mark are laid down period/ratio apart, so formants stay put. A grain's rising half always
//! mirrors the previous grain's falling half, which makes them sum to exactly one at a ratio of 1
//! even while the period changes: uncorrected audio passes through unchanged.
class PsolaShifter
{
public:
	struct Mark
	{
		double time = 0.0; //!< input sample the estimate describes
		float period = 0.f;
		float ratio = 1.f;
	};

	void setup(int maxPeriod, int latency, int hop)
	{
		m_latency = latency;
		m_hop = hop;
		m_maxPeriod = maxPeriod;
		const std::size_t size = nextPowerOfTwo(static_cast<std::size_t>(latency + 3 * maxPeriod + 64));
		for (auto& ring : m_input) { ring.assign(size, 0.f); }
		for (auto& ring : m_output) { ring.assign(size, 0.f); }
		m_weight.assign(size, 0.f);
		m_mask = size - 1;
		reset();
	}

	void reset()
	{
		for (auto& ring : m_input) { std::fill(ring.begin(), ring.end(), 0.f); }
		for (auto& ring : m_output) { std::fill(ring.begin(), ring.end(), 0.f); }
		std::fill(m_weight.begin(), m_weight.end(), 0.f);
		m_now = 0;
		m_synthesis = m_analysis = 0.0;
		m_fall = unvoicedPeriod();
		m_markCount = 0;
	}

	//! Marks must arrive in time order, one per hop
	void pushMark(const Mark& mark)
	{
		m_marks[m_markCount % m_marks.size()] = mark;
		++m_markCount;
	}

	std::int64_t now() const { return m_now; }

	//! Write one input frame; returns the shifted frame from latency() samples ago
	SampleFrame process(const SampleFrame& in)
	{
		m_input[0][m_now & m_mask] = in.left();
		m_input[1][m_now & m_mask] = in.right();
		const std::int64_t out = m_now - m_latency;
		while (m_synthesis - m_fall <= static_cast<double>(out)) { emitGrain(markAt(m_synthesis), out); }
		// Overlapping grains (shifting up) are normalised; gaps between them (shifting down) are
		// left as they are, as in classic PSOLA
		const auto i = static_cast<std::size_t>(out) & m_mask;
		const float norm = 1.f / std::max(m_weight[i], 1.f);
		const SampleFrame result(m_output[0][i] * norm, m_output[1][i] * norm);
		m_output[0][i] = m_output[1][i] = m_weight[i] = 0.f;
		++m_now;
		return result;
	}

	//! The unprocessed input lined up with process()'s output
	SampleFrame delayedInput() const
	{
		const auto i = static_cast<std::size_t>(m_now - 1 - m_latency) & m_mask;
		return SampleFrame(m_input[0][i], m_input[1][i]);
	}

private:
	Mark markAt(double time) const
	{
		if (m_markCount == 0) { return Mark{time, unvoicedPeriod(), 1.f}; }
		const auto latest = m_markCount - 1;
		const Mark& newest = m_marks[latest % m_marks.size()];
		const auto back = static_cast<std::size_t>(std::clamp((newest.time - time) / m_hop + 0.5, 0.0,
			static_cast<double>(std::min(latest, m_marks.size() - 1))));
		Mark mark = m_marks[(latest - back) % m_marks.size()];
		if (mark.period <= 0.f) { mark.period = unvoicedPeriod(); }
		mark.period = std::clamp(mark.period, 16.f, static_cast<float>(m_maxPeriod));
		return mark;
	}

	float unvoicedPeriod() const { return std::max(16.f, m_maxPeriod * 0.3f); }

	void emitGrain(const Mark& mark, std::int64_t out)
	{
		const double period = mark.period;
		// Step the input mark as far as the output mark just moved, so the two stay locked at a
		// ratio of 1; then whole periods back (repeat, shifting up) or on (skip, shifting down)
		m_analysis += m_fall;
		if (std::abs(m_synthesis - m_analysis) > static_cast<double>(m_mask)) { m_analysis = m_synthesis; }
		while (m_analysis - 0.5 * period > m_synthesis) { m_analysis -= period; }
		while (m_analysis + 0.5 * period < m_synthesis) { m_analysis += period; }
		// Never read input that hasn't arrived yet
		const double analysis = std::min(m_analysis, static_cast<double>(m_now) - period - 3.0);

		// Big shifts lose some loudness (fewer harmonics under the formants going up, gaps between
		// grains going down); make most of it up so the SHIFT knob doesn't duck the voice
		const auto gain = static_cast<float>(std::sqrt(std::max(mark.ratio, 1.f / mark.ratio)));
		const double rise = m_fall;
		const double fall = period;
		const auto first = std::max(static_cast<std::int64_t>(std::ceil(m_synthesis - rise)), out);
		const auto last = static_cast<std::int64_t>(std::floor(m_synthesis + fall));
		const double offset = analysis - m_synthesis;
		for (auto j = first; j <= last; ++j)
		{
			const double x = static_cast<double>(j) - m_synthesis;
			const auto w = static_cast<float>(0.5 + 0.5 * std::cos(std::numbers::pi * x / (x < 0.0 ? rise : fall)));
			const double position = static_cast<double>(j) + offset;
			const auto o = static_cast<std::size_t>(j) & m_mask;
			m_output[0][o] += w * gain * read(m_input[0], position);
			m_output[1][o] += w * gain * read(m_input[1], position);
			m_weight[o] += w;
		}
		m_synthesis += period / mark.ratio;
		m_fall = fall;
	}

	//! Cubic (Hermite) read at a fractional position
	float read(const std::vector<float>& ring, double position) const
	{
		const auto base = static_cast<std::int64_t>(std::floor(position));
		const auto frac = static_cast<float>(position - static_cast<double>(base));
		const auto at = [&](std::int64_t i) { return ring[static_cast<std::size_t>(i) & m_mask]; };
		const float x0 = at(base);
		if (frac == 0.f) { return x0; }
		const float xm1 = at(base - 1);
		const float x1 = at(base + 1);
		const float x2 = at(base + 2);
		const float c1 = 0.5f * (x1 - xm1);
		const float c2 = xm1 - 2.5f * x0 + 2.f * x1 - 0.5f * x2;
		const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
		return ((c3 * frac + c2) * frac + c1) * frac + x0;
	}

	std::array<std::vector<float>, 2> m_input;
	std::array<std::vector<float>, 2> m_output;
	std::vector<float> m_weight; //!< sum of the grain windows at each output sample
	std::size_t m_mask = 0;
	int m_latency = 0;
	int m_hop = 1;
	int m_maxPeriod = 1;

	std::int64_t m_now = 0;
	double m_synthesis = 0.0; //!< centre of the next output grain, in input time
	double m_analysis = 0.0;  //!< the input pitch mark nearest to it
	double m_fall = 0.0;      //!< the last grain's falling half (one input period); the next rises over it

	std::array<Mark, 256> m_marks{};
	std::size_t m_markCount = 0;
};

} // namespace lmms::figglebottomtune

#endif // LMMS_FIGGLEBOTTOM_TUNE_DSP_H
