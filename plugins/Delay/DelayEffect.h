/*
 * DelayEffect.h - declaration of DelayEffect class, the Delay plugin
 *
 * Copyright (c) 2014 David French <dave/dot/french3/at/googlemail/dot/com>
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

#ifndef DELAYEFFECT_H
#define DELAYEFFECT_H

#include <array>

#include "Effect.h"
#include "DelayControls.h"
#include "ModernDsp.h"

namespace lmms
{

class DelayEffect : public Effect
{
public:
	DelayEffect(Model* parent , const Descriptor::SubPluginFeatures::Key* key );
	~DelayEffect() override = default;

	ProcessStatus processImpl(SampleFrame* buf, const f_cnt_t frames) override;

	EffectControls* controls() override
	{
		return &m_delayControls;
	}
	void changeSampleRate();

	//! Longest delay the buffers can hold, in seconds (time + stereo offset + modulation)
	static constexpr float MaxDelaySeconds = 8.5f;

private:
	DelayControls m_delayControls;

	float m_sampleRate;
	std::array<dsp::DelayLine, 2> m_lines;
	std::array<dsp::Smoother, 2> m_timeSmoothers;
	std::array<dsp::TwoPole, 2> m_lowCut;
	std::array<dsp::TwoPole, 2> m_highCut;
	dsp::Smoother m_freezeSmoother;
	dsp::EnvelopeFollower m_duckEnvelope;
	double m_lfoPhase = 0.0;
	float m_outGain = 1.f;
};


} // namespace lmms

#endif // DELAYEFFECT_H
