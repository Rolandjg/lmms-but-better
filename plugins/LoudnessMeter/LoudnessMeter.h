/*
 * LoudnessMeter.h - EBU R128 loudness meter effect
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

#ifndef LMMS_LOUDNESS_METER_H
#define LMMS_LOUDNESS_METER_H

#include "Effect.h"
#include "LoudnessAnalyzer.h"
#include "LoudnessMeterControls.h"

namespace lmms
{

class LoudnessMeterEffect : public Effect
{
public:
	LoudnessMeterEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key);
	~LoudnessMeterEffect() override = default;

	ProcessStatus processImpl(SampleFrame* buf, const f_cnt_t frames) override;

	EffectControls* controls() override { return &m_controls; }

private:
	void resetMeasurement();

	LoudnessMeterControls m_controls;
	LoudnessAnalyzer m_analyzer;
	float m_sampleRate = 0.f;
	bool m_wasPlaying = false;
};

} // namespace lmms

#endif // LMMS_LOUDNESS_METER_H
