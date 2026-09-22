/*
 * flangereffect.h - defination of FlangerEffect class.
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


#ifndef FLANGEREFFECT_H
#define FLANGEREFFECT_H

#include <array>
#include <atomic>

#include "Effect.h"
#include "ModernDsp.h"
#include "FlangerControls.h"

namespace lmms
{



class FlangerEffect : public Effect
{
public:
	FlangerEffect( Model* parent , const Descriptor::SubPluginFeatures::Key* key );
	~FlangerEffect() override = default;

	ProcessStatus processImpl(SampleFrame* buf, const f_cnt_t frames) override;

	EffectControls* controls() override
	{
		return &m_flangerControls;
	}
	void changeSampleRate();
	void restartLFO();

	//! Current left-channel delay in seconds, published for the response display
	std::atomic<float> m_currentDelay{0.001f};

private:
	FlangerControls m_flangerControls;
	std::array<dsp::DelayLine, 2> m_delays;
	std::array<float, 2> m_lastOut{};
	double m_lfoPhase = 0.0;
	float m_sampleRate = 44100.f;
};


} // namespace lmms

#endif // FLANGEREFFECT_H
