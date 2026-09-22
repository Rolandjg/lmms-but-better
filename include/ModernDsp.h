/*
 * ModernDsp.h - small header-only DSP building blocks shared by the native plugins
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

#ifndef LMMS_MODERN_DSP_H
#define LMMS_MODERN_DSP_H

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace lmms::dsp
{

//! Topology-preserving one-pole filter providing lowpass and highpass outputs.
//! Stable under fast modulation, which makes it suitable for feedback loops.
class OnePole
{
public:
	void setCutoff(float freq, float sampleRate)
	{
		freq = std::clamp(freq, 1.f, sampleRate * 0.49f);
		const float g = std::tan(std::numbers::pi_v<float> * freq / sampleRate);
		m_g = g / (1.f + g);
	}

	float lowpass(float in)
	{
		const float v = (in - m_z) * m_g;
		const float lp = v + m_z;
		m_z = lp + v;
		return lp;
	}

	float highpass(float in) { return in - lowpass(in); }

	void reset() { m_z = 0.f; }

private:
	float m_g = 0.f;
	float m_z = 0.f;
};


//! Two cascaded one-poles: a gentle 12 dB/oct slope
class TwoPole
{
public:
	void setCutoff(float freq, float sampleRate)
	{
		m_a.setCutoff(freq, sampleRate);
		m_b.setCutoff(freq, sampleRate);
	}
	float lowpass(float in) { return m_b.lowpass(m_a.lowpass(in)); }
	float highpass(float in) { return m_b.highpass(m_a.highpass(in)); }
	void reset() { m_a.reset(); m_b.reset(); }

private:
	OnePole m_a;
	OnePole m_b;
};


//! One-pole parameter smoother (avoids zipper noise and clicks)
class Smoother
{
public:
	void setTime(float seconds, float sampleRate)
	{
		m_coeff = seconds <= 0.f ? 1.f : 1.f - std::exp(-1.f / (seconds * sampleRate));
	}
	void snap(float value) { m_value = value; }
	float next(float target)
	{
		m_value += (target - m_value) * m_coeff;
		return m_value;
	}
	float value() const { return m_value; }

private:
	float m_coeff = 1.f;
	float m_value = 0.f;
};


//! Peak envelope follower with separate attack and release
class EnvelopeFollower
{
public:
	void setTimes(float attackSeconds, float releaseSeconds, float sampleRate)
	{
		m_attack = 1.f - std::exp(-1.f / (std::max(attackSeconds, 1e-5f) * sampleRate));
		m_release = 1.f - std::exp(-1.f / (std::max(releaseSeconds, 1e-5f) * sampleRate));
	}
	float process(float in)
	{
		const float x = std::abs(in);
		m_env += (x - m_env) * (x > m_env ? m_attack : m_release);
		return m_env;
	}
	float value() const { return m_env; }
	void reset() { m_env = 0.f; }

private:
	float m_attack = 1.f;
	float m_release = 1.f;
	float m_env = 0.f;
};


//! Mono delay line with cubic (Hermite) fractional reads
class DelayLine
{
public:
	void resize(std::size_t minimumLength)
	{
		std::size_t size = 1;
		while (size < minimumLength + 4) { size <<= 1; }
		m_buffer.assign(size, 0.f);
		m_mask = size - 1;
		m_write = 0;
	}

	void clear() { std::fill(m_buffer.begin(), m_buffer.end(), 0.f); }

	std::size_t capacity() const { return m_buffer.empty() ? 0 : m_buffer.size() - 4; }

	void write(float x)
	{
		m_buffer[m_write] = x;
		m_write = (m_write + 1) & m_mask;
	}

	//! Read @p delay samples behind the most recent write (delay >= 1)
	float read(float delay) const
	{
		delay = std::clamp(delay, 1.f, static_cast<float>(capacity()));
		const float pos = static_cast<float>(m_write) - delay;
		const auto base = static_cast<long>(std::floor(pos));
		const float frac = pos - static_cast<float>(base);
		const auto at = [this](long i) { return m_buffer[static_cast<std::size_t>(i) & m_mask]; };
		const float xm1 = at(base - 1);
		const float x0 = at(base);
		const float x1 = at(base + 1);
		const float x2 = at(base + 2);
		const float c1 = 0.5f * (x1 - xm1);
		const float c2 = xm1 - 2.5f * x0 + 2.f * x1 - 0.5f * x2;
		const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
		return ((c3 * frac + c2) * frac + c1) * frac + x0;
	}

private:
	std::vector<float> m_buffer;
	std::size_t m_mask = 0;
	std::size_t m_write = 0;
};


//! Soft saturation with unity small-signal gain. drive in [0, 1]
inline float saturate(float x, float drive)
{
	if (drive <= 0.f) { return x; }
	// Blend towards a tanh curve; the small-signal gain stays at 1 so feedback loops remain stable
	const float k = 1.f + drive * 2.f;
	return (1.f - drive) * x + drive * std::tanh(x * k) / k;
}


//! Mid/side stereo width. width = 0 -> mono, 1 -> unchanged, 2 -> doubled side
inline void applyWidth(float& left, float& right, float width)
{
	const float mid = (left + right) * 0.5f;
	const float side = (left - right) * 0.5f * width;
	left = mid + side;
	right = mid - side;
}

//! RBJ-cookbook biquad (mono), for band isolation and general filtering
class Biquad
{
public:
	enum class Type { Lowpass, Highpass, Bandpass };

	void set(Type type, float freq, float q, float sampleRate)
	{
		freq = std::clamp(freq, 10.f, sampleRate * 0.45f);
		q = std::max(q, 0.05f);
		const float w0 = 2.f * std::numbers::pi_v<float> * freq / sampleRate;
		const float cosw = std::cos(w0);
		const float alpha = std::sin(w0) / (2.f * q);
		float b0, b1, b2;
		switch (type)
		{
		case Type::Lowpass: b0 = (1.f - cosw) / 2.f; b1 = 1.f - cosw; b2 = b0; break;
		case Type::Highpass: b0 = (1.f + cosw) / 2.f; b1 = -(1.f + cosw); b2 = b0; break;
		default: b0 = alpha; b1 = 0.f; b2 = -alpha; break;
		}
		const float a0 = 1.f + alpha;
		m_b0 = b0 / a0; m_b1 = b1 / a0; m_b2 = b2 / a0;
		m_a1 = -2.f * cosw / a0; m_a2 = (1.f - alpha) / a0;
	}

	float process(float x)
	{
		const float y = m_b0 * x + m_z1;
		m_z1 = m_b1 * x - m_a1 * y + m_z2;
		m_z2 = m_b2 * x - m_a2 * y;
		return y;
	}

	void reset() { m_z1 = m_z2 = 0.f; }

private:
	float m_b0 = 1.f, m_b1 = 0.f, m_b2 = 0.f, m_a1 = 0.f, m_a2 = 0.f;
	float m_z1 = 0.f, m_z2 = 0.f;
};


//! Placement of one unison voice: voices are spread evenly across the detune range and the stereo
//! field, panned with equal power and scaled by 1/sqrt(count) so a stack is as loud as one voice
struct UnisonVoice
{
	float detuneRatio = 1.f;
	float gainLeft = 1.f;
	float gainRight = 1.f;
};

inline UnisonVoice unisonVoice(int index, int count, float detuneCents, float spread)
{
	if (count <= 1) { return {}; }
	const float position = 2.f * index / (count - 1) - 1.f;
	const float pan = position * std::clamp(spread, 0.f, 1.f);
	const float angle = (pan + 1.f) * std::numbers::pi_v<float> / 4.f;
	const float gain = std::numbers::sqrt2_v<float> / std::sqrt(static_cast<float>(count));
	return {std::exp2(position * detuneCents / 1200.f), std::cos(angle) * gain, std::sin(angle) * gain};
}

} // namespace lmms::dsp

#endif // LMMS_MODERN_DSP_H
