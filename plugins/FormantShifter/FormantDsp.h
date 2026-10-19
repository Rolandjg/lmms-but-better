/*
 * FormantDsp.h - spectral formant shifting, formant-preserving pitch shifting and vowel filters
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

#ifndef LMMS_FORMANT_DSP_H
#define LMMS_FORMANT_DSP_H

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>
#include <vector>

#include <fftw3.h>

namespace lmms::formant
{

constexpr int FormantCount = 5;
constexpr int VowelCount = 5;   //!< A E I O U
constexpr int VoiceCount = 5;

struct Formant
{
	float freq;      //!< Hz
	float gainDb;
	float bandwidth; //!< Hz
};

using VowelShape = std::array<Formant, FormantCount>;

enum class Voice { Bass, Tenor, Countertenor, Alto, Soprano };

//! Sung-vowel formants (the classic Csound/IRCAM table), [voice][vowel A E I O U]
inline const VowelShape& vowelTable(int voice, int vowel)
{
	using V = VowelShape;
	static const std::array<std::array<V, VowelCount>, VoiceCount> table{{
		{{ // bass
			V{{{600, 0, 60}, {1040, -7, 70}, {2250, -9, 110}, {2450, -9, 120}, {2750, -20, 130}}},
			V{{{400, 0, 40}, {1620, -12, 80}, {2400, -9, 100}, {2800, -12, 120}, {3100, -18, 120}}},
			V{{{250, 0, 60}, {1750, -30, 90}, {2600, -16, 100}, {3050, -22, 120}, {3340, -28, 120}}},
			V{{{400, 0, 40}, {750, -11, 80}, {2400, -21, 100}, {2600, -20, 120}, {2900, -40, 120}}},
			V{{{350, 0, 40}, {600, -20, 80}, {2400, -32, 100}, {2675, -28, 120}, {2950, -36, 120}}},
		}},
		{{ // tenor
			V{{{650, 0, 80}, {1080, -6, 90}, {2650, -7, 120}, {2900, -8, 130}, {3250, -22, 140}}},
			V{{{400, 0, 70}, {1700, -14, 80}, {2600, -12, 100}, {3200, -14, 120}, {3580, -20, 120}}},
			V{{{290, 0, 40}, {1870, -15, 90}, {2800, -18, 100}, {3250, -20, 120}, {3540, -30, 120}}},
			V{{{400, 0, 40}, {800, -10, 80}, {2600, -12, 100}, {2800, -12, 120}, {3000, -26, 120}}},
			V{{{350, 0, 40}, {600, -20, 60}, {2700, -17, 100}, {2900, -14, 120}, {3300, -26, 120}}},
		}},
		{{ // countertenor
			V{{{660, 0, 80}, {1120, -6, 90}, {2750, -23, 120}, {3000, -24, 130}, {3350, -38, 140}}},
			V{{{440, 0, 70}, {1800, -14, 80}, {2700, -18, 100}, {3000, -20, 120}, {3300, -20, 120}}},
			V{{{270, 0, 40}, {1850, -24, 90}, {2900, -24, 100}, {3350, -36, 120}, {3590, -36, 120}}},
			V{{{430, 0, 40}, {820, -10, 80}, {2700, -26, 100}, {3000, -22, 120}, {3300, -34, 120}}},
			V{{{370, 0, 40}, {630, -20, 60}, {2750, -23, 100}, {3000, -30, 120}, {3400, -34, 120}}},
		}},
		{{ // alto
			V{{{800, 0, 80}, {1150, -4, 90}, {2800, -20, 120}, {3500, -36, 130}, {4950, -60, 140}}},
			V{{{400, 0, 60}, {1600, -24, 80}, {2700, -30, 120}, {3300, -35, 150}, {4950, -60, 200}}},
			V{{{350, 0, 50}, {1700, -20, 100}, {2700, -30, 120}, {3700, -36, 150}, {4950, -60, 200}}},
			V{{{450, 0, 70}, {800, -9, 80}, {2830, -16, 100}, {3500, -28, 130}, {4950, -55, 135}}},
			V{{{325, 0, 50}, {700, -12, 60}, {2530, -30, 170}, {3500, -40, 180}, {4950, -64, 200}}},
		}},
		{{ // soprano
			V{{{800, 0, 80}, {1150, -6, 90}, {2900, -32, 120}, {3900, -20, 130}, {4950, -50, 140}}},
			V{{{350, 0, 60}, {2000, -20, 100}, {2800, -15, 120}, {3600, -40, 150}, {4950, -56, 200}}},
			V{{{270, 0, 60}, {2140, -12, 90}, {2950, -26, 100}, {3900, -26, 120}, {4950, -44, 120}}},
			V{{{450, 0, 70}, {800, -11, 80}, {2830, -22, 100}, {3800, -22, 130}, {4950, -50, 135}}},
			V{{{325, 0, 50}, {700, -16, 60}, {2700, -35, 170}, {3800, -40, 180}, {4950, -60, 200}}},
		}},
	}};
	return table[std::clamp(voice, 0, VoiceCount - 1)][std::clamp(vowel, 0, VowelCount - 1)];
}


//! The formants at a continuous @p position between the vowels (0 = A ... 4 = U). Frequencies and
//! bandwidths are interpolated on a log scale, gains in dB, so a morph glides like a real mouth.
inline VowelShape vowelAt(int voice, float position)
{
	position = std::clamp(position, 0.f, static_cast<float>(VowelCount - 1));
	const int a = std::min(static_cast<int>(position), VowelCount - 2);
	const float t = position - a;
	const auto& from = vowelTable(voice, a);
	const auto& to = vowelTable(voice, a + 1);
	VowelShape out;
	for (int i = 0; i < FormantCount; ++i)
	{
		out[i].freq = from[i].freq * std::pow(to[i].freq / from[i].freq, t);
		out[i].bandwidth = from[i].bandwidth * std::pow(to[i].bandwidth / from[i].bandwidth, t);
		out[i].gainDb = from[i].gainDb + (to[i].gainDb - from[i].gainDb) * t;
	}
	return out;
}


//! Moves every formant by @p ratio (bandwidths scale along, like a longer or shorter vocal tract)
//! and widens the bandwidths by @p bandwidthScale
inline VowelShape scaledVowel(VowelShape shape, float ratio, float bandwidthScale)
{
	for (auto& f : shape)
	{
		f.freq *= ratio;
		f.bandwidth *= ratio * bandwidthScale;
	}
	return shape;
}


//! A vowel's spectral envelope, prepared for evaluating at many frequencies
class VowelEnvelope
{
public:
	explicit VowelEnvelope(const VowelShape& shape)
	{
		for (int i = 0; i < FormantCount; ++i)
		{
			m_freq[i] = shape[i].freq;
			m_invHalfWidth[i] = 2.f / std::max(shape[i].bandwidth, 1.f);
			m_amp[i] = std::pow(10.f, shape[i].gainDb / 20.f);
		}
	}

	//! Natural log of the envelope at @p freq: one resonance peak per formant (-6 dB at
	//! +-bandwidth/2), with skirts steep enough to leave real valleys between the formants
	float logAt(float freq) const
	{
		float sum = 1e-3f; // -60 dB floor between and above the formants
		for (int i = 0; i < FormantCount; ++i)
		{
			const float x = (freq - m_freq[i]) * m_invHalfWidth[i];
			sum += m_amp[i] / (1.f + x * x);
		}
		return std::log(sum);
	}

private:
	std::array<float, FormantCount> m_freq{}, m_invHalfWidth{}, m_amp{};
};


//! Sign of each band in the parallel filter. Neighbouring band-passes are in anti-phase between
//! their centres, so alternating signs (as in Klatt's parallel synthesizer) makes their skirts add
//! up instead of cancelling into deep notches.
inline float bandSign(int index) { return index % 2 == 0 ? 1.f : -1.f; }

//! Continuous-time frequency response of the resonant vowel filter (sum of normalized band-passes)
inline std::complex<float> vowelFilterResponse(const VowelShape& shape, float freq, float resonance)
{
	std::complex<float> sum = 0.f;
	for (int i = 0; i < FormantCount; ++i)
	{
		const auto& f = shape[i];
		const float q = std::clamp(f.freq / std::max(f.bandwidth, 1.f) * resonance, 0.5f, 60.f);
		const std::complex<float> s(0.f, freq / f.freq);
		sum += bandSign(i) * std::pow(10.f, f.gainDb / 20.f) * (s / q) / (s * s + s / q + 1.f);
	}
	return sum;
}


//! Output gain that keeps the resonant filter about as loud as its input for broadband material
inline float vowelFilterMakeup(const VowelShape& shape, float resonance)
{
	// A band-pass passes roughly (pi/2 * bandwidth / freq) of a pink spectrum's ~10 octaves
	float passed = 0.f;
	for (const auto& f : shape)
	{
		const float q = std::clamp(f.freq / std::max(f.bandwidth, 1.f) * resonance, 0.5f, 60.f);
		const float g = std::pow(10.f, f.gainDb / 20.f);
		passed += g * g * (std::numbers::pi_v<float> / 2.f) / q;
	}
	return std::clamp(0.8f / std::sqrt(passed / 6.9f), 1.f, 6.f);
}


//! Five parallel resonant band-passes (TPT state-variable filters), the classic formant filter
class ResonantVowelFilter
{
public:
	void setSampleRate(float sampleRate) { m_sampleRate = sampleRate; }

	//! Call once per small block; coefficient changes are click-free with this filter structure
	void set(const VowelShape& shape, float resonance)
	{
		for (int i = 0; i < FormantCount; ++i)
		{
			const auto& f = shape[i];
			auto& b = m_bands[i];
			const float freq = std::clamp(f.freq, 20.f, m_sampleRate * 0.45f);
			const float q = std::clamp(f.freq / std::max(f.bandwidth, 1.f) * resonance, 0.5f, 60.f);
			const float g = std::tan(std::numbers::pi_v<float> * freq / m_sampleRate);
			b.k = 1.f / q;
			b.a1 = 1.f / (1.f + g * (g + b.k));
			b.a2 = g * b.a1;
			b.a3 = g * b.a2;
			// Formants pushed above the Nyquist fade out instead of piling up at the top
			const float fade = std::clamp((m_sampleRate * 0.45f - f.freq) / (m_sampleRate * 0.05f), 0.f, 1.f);
			b.gain = bandSign(i) * std::pow(10.f, f.gainDb / 20.f) * fade;
		}
		m_makeup = vowelFilterMakeup(shape, resonance);
	}

	float process(float x)
	{
		float out = 0.f;
		for (auto& b : m_bands)
		{
			const float v3 = x - b.ic2;
			const float v1 = b.a1 * b.ic1 + b.a2 * v3;
			const float v2 = b.ic2 + b.a2 * b.ic1 + b.a3 * v3;
			b.ic1 = 2.f * v1 - b.ic1;
			b.ic2 = 2.f * v2 - b.ic2;
			out += b.gain * b.k * v1;
		}
		return out * m_makeup;
	}

	void reset()
	{
		for (auto& b : m_bands) { b.ic1 = b.ic2 = 0.f; }
	}

private:
	struct Band
	{
		float a1 = 0.f, a2 = 0.f, a3 = 0.f, k = 1.f, gain = 0.f;
		float ic1 = 0.f, ic2 = 0.f;
	};
	std::array<Band, FormantCount> m_bands;
	float m_sampleRate = 44100.f;
	float m_makeup = 1.f;
};


//! Streaming STFT engine for one channel. Per frame it estimates the spectral envelope (true
//! envelope by iterated cepstral smoothing), optionally pitch-shifts the partials (phase-locked
//! peak shifting after Laroche & Dolson), and then reshapes the spectrum so the formants end up
//! where they should: shifted by the formant ratio, and/or replaced by a vowel ("imprint").
//! Plans and buffers are created in the constructor; nothing allocates on the audio thread.
class SpectralEngine
{
public:
	static constexpr int MaxSize = 4096;
	//! Formant changes fade in between these frequencies (Hz)
	static constexpr float LowProtectStart = 60.f;
	static constexpr float LowProtectEnd = 200.f;

	struct Settings
	{
		float formantRatio = 1.f; //!< where the input's own formants go (already includes the pitch ratio when not preserved)
		float pitchRatio = 1.f;
		float imprint = 0.f;      //!< 0 = keep the sound's own formants, 1 = fully replaced by the vowel
		VowelShape vowel = vowelTable(1, 0);
	};

	SpectralEngine()
	{
		m_time = static_cast<float*>(fftwf_malloc(sizeof(float) * MaxSize));
		m_spec = static_cast<fftwf_complex*>(fftwf_malloc(sizeof(fftwf_complex) * (MaxSize / 2 + 1)));
		for (int i = 0; i < 2; ++i)
		{
			const int n = MaxSize >> (1 - i);
			m_forward[i] = fftwf_plan_dft_r2c_1d(n, m_time, m_spec, FFTW_ESTIMATE);
			m_inverse[i] = fftwf_plan_dft_c2r_1d(n, m_spec, m_time, FFTW_ESTIMATE);
		}
		const std::size_t bins = MaxSize / 2 + 1;
		for (auto* v : {&m_mag, &m_phase, &m_prevPhase, &m_prevRot, &m_newRot, &m_logMag, &m_env,
			&m_work, &m_gain, &m_outEnv})
		{
			v->assign(bins, 0.f);
		}
		m_x.assign(bins, 0.f);
		m_y.assign(bins, 0.f);
		m_peaks.reserve(bins);
		m_inFifo.assign(MaxSize, 0.f);
		m_outFifo.assign(MaxSize, 0.f);
		m_accum.assign(MaxSize * 2, 0.f);
		m_window.assign(MaxSize, 0.f);
		m_lifter.assign(MaxSize, 0.f);
		setSampleRate(44100.f);
	}

	~SpectralEngine()
	{
		for (int i = 0; i < 2; ++i)
		{
			fftwf_destroy_plan(m_forward[i]);
			fftwf_destroy_plan(m_inverse[i]);
		}
		fftwf_free(m_time);
		fftwf_free(m_spec);
	}

	SpectralEngine(const SpectralEngine&) = delete;
	SpectralEngine& operator=(const SpectralEngine&) = delete;

	//! Picks a ~43 ms frame (2048 at 44.1/48 kHz, 4096 above) and resets. Does not allocate.
	void setSampleRate(float sampleRate)
	{
		m_sampleRate = sampleRate;
		m_plan = sampleRate > 60000.f ? 1 : 0;
		m_size = MaxSize >> (1 - m_plan);
		m_hop = m_size / 4;
		for (int n = 0; n < m_size; ++n)
		{
			m_window[n] = 0.5f - 0.5f * std::cos(2.f * std::numbers::pi_v<float> * n / m_size);
		}
		// Keep quefrencies below ~1.2 ms: smooth enough to step over the harmonics of voices up to
		// ~800 Hz, detailed enough for formants. Raised-cosine taper to avoid ripples in the envelope.
		const int cutoff = std::max(8, static_cast<int>(sampleRate * 0.0012f));
		std::fill(m_lifter.begin(), m_lifter.end(), 0.f);
		for (int n = 0; n <= cutoff && n < m_size / 2; ++n)
		{
			const float t = std::clamp((n - cutoff * 0.5f) / (cutoff * 0.5f), 0.f, 1.f);
			const float w = (0.5f + 0.5f * std::cos(std::numbers::pi_v<float> * t)) / m_size;
			m_lifter[n] = w;
			if (n > 0) { m_lifter[m_size - n] = w; }
		}
		reset();
	}

	void reset()
	{
		std::fill(m_inFifo.begin(), m_inFifo.end(), 0.f);
		std::fill(m_outFifo.begin(), m_outFifo.end(), 0.f);
		std::fill(m_accum.begin(), m_accum.end(), 0.f);
		std::fill(m_prevPhase.begin(), m_prevPhase.end(), 0.f);
		std::fill(m_prevRot.begin(), m_prevRot.end(), 0.f);
		std::fill(m_env.begin(), m_env.end(), std::log(1e-6f));
		std::fill(m_logMag.begin(), m_logMag.end(), std::log(1e-6f));
		std::fill(m_outEnv.begin(), m_outEnv.end(), std::log(1e-6f));
		m_rover = m_size - m_hop;
		m_imprintOffset = 0.f;
		m_offsetValid = false;
	}

	int size() const { return m_size; }
	int bins() const { return m_size / 2 + 1; }
	//! A sample is final once the last frame overlapping it has been processed, a whole frame later
	int latency() const { return m_size; }
	float sampleRate() const { return m_sampleRate; }

	void setSettings(const Settings& settings) { m_settings = settings; }

	float process(float in)
	{
		const int start = m_size - m_hop;
		m_inFifo[m_rover] = in;
		const float out = m_outFifo[m_rover - start];
		if (++m_rover >= m_size)
		{
			m_rover = start;
			processFrame();
		}
		return out;
	}

	//! Counts processed frames, so a display knows when the analysis below changed
	std::uint32_t frameCount() const { return m_frames; }
	//! Last frame's input spectrum, its envelope and the envelope after processing (natural log, per bin)
	const std::vector<float>& inputSpectrum() const { return m_logMag; }
	const std::vector<float>& inputEnvelope() const { return m_env; }
	const std::vector<float>& outputEnvelope() const { return m_outEnv; }

private:
	static float wrapPhase(float p)
	{
		constexpr float pi = std::numbers::pi_v<float>;
		return p - 2.f * pi * std::floor((p + pi) / (2.f * pi));
	}

	//! Linear interpolation of the input envelope at a fractional bin
	float envelopeAt(float bin) const
	{
		const int last = bins() - 1;
		bin = std::clamp(bin, 0.f, static_cast<float>(last));
		const int i = std::min(static_cast<int>(bin), last - 1);
		const float t = bin - i;
		return m_env[i] + (m_env[i + 1] - m_env[i]) * t;
	}

	//! True envelope (Röbel & Rodet): smooth the log spectrum in the cepstral domain, then lift it
	//! to hug the peaks, so it follows the formants instead of sagging between harmonics
	void trueEnvelope()
	{
		const int n = bins();
		std::copy(m_logMag.begin(), m_logMag.begin() + n, m_work.begin());
		constexpr int Iterations = 6;
		for (int it = 0; it < Iterations; ++it)
		{
			for (int k = 0; k < n; ++k)
			{
				m_spec[k][0] = m_work[k];
				m_spec[k][1] = 0.f;
			}
			fftwf_execute(m_inverse[m_plan]);
			for (int i = 0; i < m_size; ++i) { m_time[i] *= m_lifter[i]; }
			fftwf_execute(m_forward[m_plan]);
			for (int k = 0; k < n; ++k)
			{
				m_env[k] = m_spec[k][0];
				m_work[k] = std::max(m_logMag[k], m_env[k]);
			}
		}
	}

	//! Moves every spectral peak and its region of influence to its new frequency, rotating the
	//! phases so each partial keeps advancing at exactly the shifted rate (phase-locked vocoder)
	void shiftPitch(float ratio)
	{
		const int n = bins();
		std::fill(m_y.begin(), m_y.begin() + n, std::complex<float>(0.f));
		float maxMag = 0.f;
		for (int k = 0; k < n; ++k) { maxMag = std::max(maxMag, m_mag[k]); }
		const float threshold = std::max(maxMag * 1e-4f, 1e-9f);
		m_peaks.clear();
		for (int k = 2; k < n - 2; ++k)
		{
			const float m = m_mag[k];
			if (m > threshold && m > m_mag[k - 1] && m >= m_mag[k + 1] && m > m_mag[k - 2] && m >= m_mag[k + 2])
			{
				m_peaks.push_back(k);
			}
		}
		std::fill(m_newRot.begin(), m_newRot.begin() + n, 0.f);
		const float binToOmega = 2.f * std::numbers::pi_v<float> / m_size;
		int regionStart = 0;
		for (std::size_t j = 0; j < m_peaks.size(); ++j)
		{
			const int p = m_peaks[j];
			int regionEnd = n - 1;
			if (j + 1 < m_peaks.size())
			{
				// The region ends at the quietest bin between this peak and the next
				const int next = m_peaks[j + 1];
				regionEnd = p;
				for (int k = p + 1; k < next; ++k) { if (m_mag[k] < m_mag[regionEnd]) { regionEnd = k; } }
				if (regionEnd == p) { regionEnd = (p + next) / 2; }
			}
			// Instantaneous frequency of the peak from the phase advance since the last frame
			const float expected = binToOmega * p * m_hop;
			const float deviation = wrapPhase(m_phase[p] - m_prevPhase[p] - expected);
			const float trueBin = p + deviation / (binToOmega * m_hop);
			const float targetBin = trueBin * ratio;
			const int shift = static_cast<int>(std::lround(targetBin - p));
			const float theta = wrapPhase(m_prevRot[p] + (targetBin - trueBin) * binToOmega * m_hop);
			const std::complex<float> rotation = std::polar(1.f, theta);
			for (int k = regionStart; k <= regionEnd; ++k)
			{
				m_newRot[k] = theta;
				const int t = k + shift;
				if (t >= 0 && t < n) { m_y[t] += m_x[k] * rotation; }
			}
			regionStart = regionEnd + 1;
		}
		std::copy(m_newRot.begin(), m_newRot.begin() + n, m_prevRot.begin());
	}

	void processFrame()
	{
		const int n = bins();
		const int half = m_size / 2;
		const auto& s = m_settings;

		// Zero-phase framing (window centred on sample 0) keeps the phases of a peak's main lobe
		// aligned, which the bin shifting relies on
		for (int i = 0; i < m_size; ++i) { m_time[(i + half) % m_size] = m_inFifo[i] * m_window[i]; }
		fftwf_execute(m_forward[m_plan]);
		constexpr float Floor = 1e-7f;
		for (int k = 0; k < n; ++k)
		{
			m_x[k] = {m_spec[k][0], m_spec[k][1]};
			m_mag[k] = std::abs(m_x[k]);
			m_phase[k] = std::arg(m_x[k]);
			m_logMag[k] = std::log(m_mag[k] + Floor);
		}

		trueEnvelope();

		const float pitchRatio = std::clamp(s.pitchRatio, 0.25f, 4.f);
		const bool shifting = std::abs(pitchRatio - 1.f) > 1e-4f;
		if (shifting) { shiftPitch(pitchRatio); }
		else
		{
			std::copy(m_x.begin(), m_x.begin() + n, m_y.begin());
			std::fill(m_prevRot.begin(), m_prevRot.begin() + n, 0.f);
		}
		std::copy(m_phase.begin(), m_phase.begin() + n, m_prevPhase.begin());

		// Spectral gain per bin: the envelope we want divided by the envelope the bin has now
		const float formantRatio = std::clamp(s.formantRatio, 0.125f, 8.f);
		const float imprint = std::clamp(s.imprint, 0.f, 1.f);
		const float binHz = m_sampleRate / m_size;
		constexpr float MinGain = -6.9f;  // -60 dB
		constexpr float MaxGain = 3.2f;   // +28 dB: don't pull noise out of empty regions
		const VowelEnvelope vowel(s.vowel);
		double weightSum = 0.0;
		double differenceSum = 0.0;
		for (int k = 0; k < n; ++k)
		{
			const float current = shifting ? envelopeAt(k / pitchRatio) : m_env[k];
			const float own = envelopeAt(k / formantRatio) - current;
			m_gain[k] = own;
			if (imprint > 0.f)
			{
				// m_work holds the vowel's log envelope relative to the current one
				m_work[k] = vowel.logAt(k * binHz) - current;
				const double power = std::norm(m_y[k]);
				weightSum += power;
				differenceSum += power * (own - m_work[k]);
			}
		}
		if (imprint > 0.f)
		{
			// Level the vowel against the sound's own formants: first where the sound has its energy
			// (a power-weighted mean in dB), then fine-tuned so both come out equally loud. Smoothed
			// over a few frames so it doesn't flutter.
			if (weightSum > 1e-20)
			{
				float offset = static_cast<float>(differenceSum / weightSum);
				double ownEnergy = 0.0;
				double vowelEnergy = 0.0;
				for (int k = 0; k < n; ++k)
				{
					const double power = std::norm(m_y[k]);
					ownEnergy += power * std::exp(2.f * std::clamp(m_gain[k], MinGain, MaxGain));
					vowelEnergy += power * std::exp(2.f * std::clamp(m_work[k] + offset, MinGain, MaxGain));
				}
				if (ownEnergy > 1e-20 && vowelEnergy > 1e-20)
				{
					offset += static_cast<float>(0.5 * std::log(ownEnergy / vowelEnergy));
				}
				m_imprintOffset = m_offsetValid ? m_imprintOffset + (offset - m_imprintOffset) * 0.35f : offset;
				m_offsetValid = true;
			}
			for (int k = 0; k < n; ++k)
			{
				m_gain[k] = (1.f - imprint) * m_gain[k] + imprint * (m_work[k] + m_imprintOffset);
			}
		}

		// Leave the lowest octaves alone: they hold fundamentals rather than formants, and the
		// envelope there is dominated by its mirror image around 0 Hz
		const int protectStart = static_cast<int>(LowProtectStart / binHz);
		const int protectEnd = static_cast<int>(LowProtectEnd / binHz) + 1;
		for (int k = 0; k < std::min(protectEnd, n); ++k)
		{
			const float t = std::clamp(static_cast<float>(k - protectStart) / (protectEnd - protectStart), 0.f, 1.f);
			m_gain[k] *= t * t * (3.f - 2.f * t);
		}

		for (int k = 0; k < n; ++k)
		{
			const float g = std::clamp(m_gain[k], MinGain, MaxGain);
			const float current = shifting ? envelopeAt(k / pitchRatio) : m_env[k];
			m_outEnv[k] = current + g;
			const std::complex<float> y = m_y[k] * std::exp(g);
			m_spec[k][0] = y.real();
			m_spec[k][1] = y.imag();
		}
		// Nothing meaningful lives at DC and Nyquist for a shifted signal
		m_spec[0][1] = 0.f;
		m_spec[n - 1][1] = 0.f;

		fftwf_execute(m_inverse[m_plan]);

		// Hann analysis and synthesis at 75 % overlap sum to 1.5
		const float norm = 1.f / (m_size * 1.5f);
		for (int i = 0; i < m_size; ++i)
		{
			m_accum[i] += m_time[(i + half) % m_size] * m_window[i] * norm;
		}
		std::copy(m_accum.begin(), m_accum.begin() + m_hop, m_outFifo.begin());
		std::copy(m_accum.begin() + m_hop, m_accum.begin() + m_hop + m_size, m_accum.begin());
		std::copy(m_inFifo.begin() + m_hop, m_inFifo.begin() + m_size, m_inFifo.begin());
		++m_frames;
	}

	float* m_time = nullptr;
	fftwf_complex* m_spec = nullptr;
	std::array<fftwf_plan, 2> m_forward{};
	std::array<fftwf_plan, 2> m_inverse{};
	int m_plan = 0;
	int m_size = 2048;
	int m_hop = 512;
	int m_rover = 0;
	float m_sampleRate = 44100.f;
	Settings m_settings;

	std::vector<float> m_inFifo, m_outFifo, m_accum, m_window, m_lifter;
	std::vector<std::complex<float>> m_x, m_y;
	std::vector<float> m_mag, m_phase, m_prevPhase, m_prevRot, m_newRot, m_logMag, m_env, m_work, m_gain, m_outEnv;
	std::vector<int> m_peaks;
	float m_imprintOffset = 0.f;
	bool m_offsetValid = false;
	std::uint32_t m_frames = 0;
};


//! Low-frequency oscillator shapes for the vowel/formant modulation
enum class LfoShape { Sine, Triangle, SawUp, SawDown, Square, SampleHold, SmoothRandom };

//! Bipolar (-1..1) LFO value at @p phase (in cycles; the integer part picks the random values,
//! so random shapes repeat identically when the phase is derived from the song position)
inline float lfoValue(LfoShape shape, double phase)
{
	const double cycle = std::floor(phase);
	const float t = static_cast<float>(phase - cycle);
	const auto random = [](std::int64_t i) {
		auto x = static_cast<std::uint32_t>(i * 2654435761u + 0x9e3779b9u);
		x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
		return static_cast<float>(x) / 4294967295.f * 2.f - 1.f;
	};
	switch (shape)
	{
	case LfoShape::Sine: return std::sin(2.f * std::numbers::pi_v<float> * t);
	case LfoShape::Triangle: return t < 0.25f ? 4.f * t : t < 0.75f ? 2.f - 4.f * t : 4.f * t - 4.f;
	case LfoShape::SawUp: return 2.f * t - 1.f;
	case LfoShape::SawDown: return 1.f - 2.f * t;
	case LfoShape::Square: return t < 0.5f ? 1.f : -1.f;
	case LfoShape::SampleHold: return random(static_cast<std::int64_t>(cycle));
	case LfoShape::SmoothRandom:
	{
		const float a = random(static_cast<std::int64_t>(cycle));
		const float b = random(static_cast<std::int64_t>(cycle) + 1);
		const float s = 0.5f - 0.5f * std::cos(std::numbers::pi_v<float> * t);
		return a + (b - a) * s;
	}
	}
	return 0.f;
}

} // namespace lmms::formant

#endif // LMMS_FORMANT_DSP_H
