/*
 * FigglebottomTune.h - pitch correction that snaps a voice to a key and scale
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

#ifndef LMMS_FIGGLEBOTTOM_TUNE_H
#define LMMS_FIGGLEBOTTOM_TUNE_H

#include "Effect.h"
#include "FigglebottomTuneControls.h"
#include "FigglebottomTuneDsp.h"

namespace lmms
{

class FigglebottomTuneEffect : public Effect
{
public:
	FigglebottomTuneEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key);
	~FigglebottomTuneEffect() override = default;

	ProcessStatus processImpl(SampleFrame* buf, const f_cnt_t frames) override;

	EffectControls* controls() override { return &m_controls; }

	//! Grains need two input periods of the lowest trackable pitch ahead of them
	f_cnt_t latency() const override;

private:
	void setSampleRate(float sampleRate);
	figglebottomtune::CorrectorSettings settings() const;

	FigglebottomTuneControls m_controls;
	figglebottomtune::PitchDetector m_detector;
	figglebottomtune::Corrector m_corrector;
	figglebottomtune::PsolaShifter m_shifter;
	float m_sampleRate = 0.f;
};

} // namespace lmms

#endif // LMMS_FIGGLEBOTTOM_TUNE_H
