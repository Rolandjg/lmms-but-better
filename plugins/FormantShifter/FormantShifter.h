/*
 * FormantShifter.h - formant shifter, formant-preserving pitch shifter and vowel filter
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

#ifndef LMMS_FORMANT_SHIFTER_H
#define LMMS_FORMANT_SHIFTER_H

#include <array>
#include <vector>

#include "Effect.h"
#include "FormantDsp.h"
#include "FormantShifterControls.h"
#include "ModernDsp.h"

namespace lmms
{

class FormantShifterEffect : public Effect
{
public:
	FormantShifterEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key);
	~FormantShifterEffect() override = default;

	ProcessStatus processImpl(SampleFrame* buf, const f_cnt_t frames) override;

	EffectControls* controls() override { return &m_controls; }

	//! The spectral engine delays by one analysis frame; the resonant filter has no latency
	f_cnt_t latency() const override;

private:
	void resetState();
	void publishEnvelopes();

	FormantShifterControls m_controls;
	std::array<formant::SpectralEngine, 2> m_engines;
	std::array<formant::ResonantVowelFilter, 2> m_filters;
	dsp::EnvelopeFollower m_follower;

	//! Dry signal, delayed to line up with the spectral engine's output
	std::vector<SampleFrame> m_dry;
	std::size_t m_dryWrite = 0;

	float m_sampleRate = 0.f;
	int m_mode = -1;
	double m_lfoPhase = 0.0;     //!< free-running LFO phase in cycles
	float m_vowel = 0.f;         //!< smoothed, modulated vowel position
	float m_formant = 0.f;       //!< smoothed, modulated formant shift in semitones
	bool m_smoothersPrimed = false;
	std::uint32_t m_publishedFrame = 0;
};

} // namespace lmms

#endif // LMMS_FORMANT_SHIFTER_H
