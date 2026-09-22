/*
 * TimeStretch.h - offline WSOLA time-stretching (tempo change without pitch change)
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

#ifndef LMMS_TIME_STRETCH_H
#define LMMS_TIME_STRETCH_H

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include "SampleFrame.h"

namespace lmms::dsp
{

/**
 * Waveform-similarity overlap-add (WSOLA) time stretching.
 *
 * Windowed grains are read from the input at the analysis hop and written at the synthesis hop.
 * Every grain position is nudged within a small search range so that it lines up with the natural
 * continuation of the previous grain, which avoids the phasing of plain overlap-add. It works
 * well for drums, bass lines and most loops and needs no external library.
 *
 * @param lengthRatio output length / input length (2 = twice as long = half the tempo)
 */
inline std::vector<SampleFrame> timeStretch(const SampleFrame* input, std::size_t inputFrames,
	double lengthRatio, int sampleRate)
{
	if (inputFrames == 0 || lengthRatio <= 0.0) { return {}; }
	const auto outputFrames = static_cast<std::size_t>(std::llround(inputFrames * lengthRatio));
	if (std::abs(lengthRatio - 1.0) < 1e-4) { return {input, input + inputFrames}; }

	// 30 ms grains keep transients reasonably tight while still resolving bass
	const int window = std::max(64, static_cast<int>(0.03 * sampleRate)) & ~1;
	const int synthesisHop = window / 2;
	const double analysisHop = synthesisHop / lengthRatio;
	const int tolerance = window / 4;
	const int overlap = window / 2;

	if (inputFrames < static_cast<std::size_t>(window + 2 * tolerance))
	{
		// Too short to stretch meaningfully: resample instead
		std::vector<SampleFrame> out(outputFrames);
		for (std::size_t i = 0; i < outputFrames; ++i)
		{
			out[i] = input[std::min(inputFrames - 1, static_cast<std::size_t>(i / lengthRatio))];
		}
		return out;
	}

	std::vector<float> hann(window);
	for (int i = 0; i < window; ++i)
	{
		hann[i] = 0.5f - 0.5f * std::cos(2.f * std::numbers::pi_v<float> * i / window);
	}

	std::vector<SampleFrame> out(outputFrames + window);
	std::vector<float> norm(outputFrames + window, 0.f);
	const auto lastStart = static_cast<long>(inputFrames) - window;
	auto mono = [input](long i) { return input[i].left() + input[i].right(); };

	long previous = 0;
	for (std::size_t outPos = 0; outPos < outputFrames; outPos += synthesisHop)
	{
		const auto nominal = static_cast<long>(outPos / static_cast<double>(synthesisHop) * analysisHop);
		long best = std::clamp(nominal, 0L, lastStart);
		if (outPos > 0)
		{
			// Where the previous grain would naturally continue; find the candidate that matches it best
			const long natural = std::clamp(previous + synthesisHop, 0L, lastStart);
			double bestScore = -1e30;
			const long from = std::clamp(nominal - tolerance, 0L, lastStart);
			const long to = std::clamp(nominal + tolerance, 0L, lastStart);
			for (long candidate = from; candidate <= to; candidate += 2)
			{
				double score = 0.0;
				for (int i = 0; i < overlap; i += 2)
				{
					score += mono(candidate + i) * mono(natural + i);
				}
				if (score > bestScore) { bestScore = score; best = candidate; }
			}
		}
		previous = best;

		for (int i = 0; i < window; ++i)
		{
			out[outPos + i] += input[best + i] * hann[i];
			norm[outPos + i] += hann[i];
		}
	}

	out.resize(outputFrames);
	for (std::size_t i = 0; i < outputFrames; ++i)
	{
		if (norm[i] > 1e-3f) { out[i] *= 1.f / norm[i]; }
	}
	return out;
}

} // namespace lmms::dsp

#endif // LMMS_TIME_STRETCH_H
