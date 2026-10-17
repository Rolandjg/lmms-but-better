/*
 * Pumper.h - tempo-synced volume shaper for sidechain style pumping
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

#ifndef LMMS_PUMPER_H
#define LMMS_PUMPER_H

#include <array>

#include "Effect.h"
#include "ModernDsp.h"
#include "PumperControls.h"

namespace lmms
{

class PumperEffect : public Effect
{
public:
	PumperEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key);
	~PumperEffect() override = default;

	ProcessStatus processImpl(SampleFrame* buf, const f_cnt_t frames) override;

	EffectControls* controls() override { return &m_controls; }

private:
	PumperControls m_controls;
	std::array<dsp::TwoPole, 2> m_split;
	dsp::Smoother m_gainSmoother;
	double m_freePhaseTicks = 0.0; //!< used while the song is stopped

	friend class PumperControls;
};

} // namespace lmms

#endif // LMMS_PUMPER_H
