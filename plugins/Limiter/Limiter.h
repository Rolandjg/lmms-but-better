/*
 * Limiter.h - lookahead true-peak brickwall limiter for mastering
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

#ifndef LMMS_LIMITER_H
#define LMMS_LIMITER_H

#include "Effect.h"
#include "LimiterControls.h"
#include "LimiterDsp.h"

namespace lmms
{

class LimiterEffect : public Effect
{
public:
	LimiterEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key);
	~LimiterEffect() override = default;

	ProcessStatus processImpl(SampleFrame* buf, const f_cnt_t frames) override;

	EffectControls* controls() override { return &m_controls; }

private:
	LimiterControls m_controls;
	LimiterDsp m_dsp;
	std::vector<float> m_dryDelay;
	std::size_t m_dryWrite = 0;

	// Accumulators for the next history column
	int m_historyFrames = 0;
	float m_historyIn = 0.f;
	float m_historyOut = 0.f;
	float m_historyGain = 1.f;

	friend class LimiterControls;
};

} // namespace lmms

#endif // LMMS_LIMITER_H
