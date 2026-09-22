/*
 * flangereffect.cpp - defination of FlangerEffect class.
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

#include "FlangerEffect.h"

#include <numbers>

#include "Engine.h"

#include "embed.h"
#include "lmms_math.h"
#include "plugin_export.h"

namespace lmms
{


extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT flanger_plugin_descriptor =
{
	LMMS_STRINGIFY( PLUGIN_NAME ),
	"Flanger",
	QT_TRANSLATE_NOOP( "PluginBrowser", "Smooth stereo flanger with sine / triangle sweep and mix" ),
	"Dave French <contact/dot/dave/dot/french3/at/googlemail/dot/com>",
	0x0100,
	Plugin::Type::Effect,
	new PixmapLoader("lmms-plugin-logo"),
	nullptr,
	nullptr,
} ;




FlangerEffect::FlangerEffect( Model *parent, const Plugin::Descriptor::SubPluginFeatures::Key *key ) :
	Effect( &flanger_plugin_descriptor, parent, key ),
	m_flangerControls( this )
{
	changeSampleRate();
}




Effect::ProcessStatus FlangerEffect::processImpl(SampleFrame* buf, const f_cnt_t frames)
{
	auto& c = m_flangerControls;
	const float d = dryLevel();
	const float w = wetLevel();
	const float sr = m_sampleRate;
	const float length = c.m_delayTimeModel.value() * sr;
	const float noise = c.m_whiteNoiseAmountModel.value();
	const float amplitude = c.m_lfoAmountModel.value() * sr;
	const bool cross = c.m_invertFeedbackModel.value();
	const float feedback = c.m_feedbackModel.value();
	const float mix = c.m_mixModel.value() * 0.01f;
	const auto shape = static_cast<FlangerControls::Shape>(c.m_shapeModel.value());
	const double increment = 1.0 / (std::max(c.m_lfoFrequencyModel.value(), 0.0001f) * sr);
	const double stereoOffset = c.m_lfoPhaseModel.value() / 360.0;

	// Unipolar LFO in [0, 1]; phase is in cycles
	auto lfo = [shape](double phase)
	{
		phase -= std::floor(phase);
		return shape == FlangerControls::Shape::Sine
			? 0.5f + 0.5f * static_cast<float>(std::sin(2.0 * std::numbers::pi * phase))
			: static_cast<float>(1.0 - 2.0 * std::abs(phase - 0.5));
	};

	for( f_cnt_t f = 0; f < frames; ++f )
	{
		std::array<float, 2> in = {
			buf[f][0] + fastRandInc(-1.f, 1.f) * noise,
			buf[f][1] + fastRandInc(-1.f, 1.f) * noise
		};
		const auto dry = in;

		m_lfoPhase += increment;
		if (m_lfoPhase >= 1.0) { m_lfoPhase -= 1.0; }
		const std::array<float, 2> times = {
			length + 2.f * amplitude * lfo(m_lfoPhase),
			length + 2.f * amplitude * lfo(m_lfoPhase + stereoOffset)
		};

		// "Invert" crosses the channels: each delay line is fed by the opposite input
		if (cross) { std::swap(in[0], in[1]); }

		std::array<float, 2> wet;
		for (auto ch = 0; ch < 2; ++ch)
		{
			// Fractional reads keep the sweep smooth instead of stepping sample by sample
			wet[ch] = m_delays[ch].read(std::max(times[ch], 1.f));
			m_delays[ch].write(in[ch] + std::clamp(wet[ch] * feedback, -4.f, 4.f));
		}

		// Summing dry and delayed signal produces the comb notches that define flanging
		const float outL = dry[0] * (1.f - mix) + wet[0] * mix;
		const float outR = dry[1] * (1.f - mix) + wet[1] * mix;

		buf[f][0] = d * dry[0] + w * outL;
		buf[f][1] = d * dry[1] + w * outR;
	}

	m_currentDelay.store(length / sr + 2.f * c.m_lfoAmountModel.value() * lfo(m_lfoPhase), std::memory_order_relaxed);

	return ProcessStatus::ContinueIfNotQuiet;
}




void FlangerEffect::changeSampleRate()
{
	m_sampleRate = Engine::audioEngine()->outputSampleRate();
	// Base delay (50 ms) + two times the maximum sweep depth
	for (auto& line : m_delays) { line.resize(static_cast<std::size_t>(0.06f * m_sampleRate) + 8); }
}




void FlangerEffect::restartLFO()
{
	m_lfoPhase = 0.0;
}




extern "C"
{

//needed for getting plugin out of shared lib
PLUGIN_EXPORT Plugin * lmms_plugin_main( Model* parent, void* data )
{
	return new FlangerEffect( parent , static_cast<const Plugin::Descriptor::SubPluginFeatures::Key *>( data ) );
}

}}


} // namespace lmms
