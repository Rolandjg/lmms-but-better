/*
 * peak_controller_Effect.h - PeakController effect plugin
 *
 * Copyright (c) 2008 Paul Giblock <drfaygo/at/gmail/dot/com>
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


#ifndef PEAK_CONTROLLER_EFFECT_H
#define PEAK_CONTROLLER_EFFECT_H

#include <array>
#include <atomic>

#include "Effect.h"
#include "PeakControllerEffectControls.h"

namespace lmms
{


class PeakController;

class PeakControllerEffect : public Effect
{
public:
	PeakControllerEffect( Model * parent, 
						const Descriptor::SubPluginFeatures::Key * _key );
	~PeakControllerEffect() override;

	ProcessStatus processImpl(SampleFrame* buf, const f_cnt_t frames) override;

	EffectControls * controls() override
	{
		return &m_peakControls;
	}

	float lastSample()
	{
		return m_lastSample;
	}

	PeakController * controller()
	{
		return m_autoController;
	}
	
	FloatModel * attackModel()
	{
		return &( m_peakControls.m_attackModel );
	}

	FloatModel * decayModel()
	{
		return &( m_peakControls.m_decayModel );
	}

	int m_effectId;

	//! Per-period history for the GUI: the measured input level (before the threshold) and the
	//! controller value after attack/release smoothing
	static constexpr std::size_t HistorySize = 512;
	std::array<float, HistorySize> m_inputHistory{};
	std::array<float, HistorySize> m_outputHistory{};
	std::atomic<std::size_t> m_historyPos{0};

	//! Time constant in ms of PeakController's smoothing for an attack or release knob value
	static float smoothingTimeMs(float knobValue);

private:
	PeakControllerEffectControls m_peakControls;

	float m_lastSample;
	float m_smoothedSample = 0.f;

	PeakController * m_autoController;

	friend class PeakControllerEffectControls;

} ;


} // namespace lmms

#endif
