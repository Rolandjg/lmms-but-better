/*
 * CrossoverEQ.cpp - A native 4-band Crossover Equalizer 
 * good for simulating tonestacks or simple peakless (flat-band) equalization
 *
 * Copyright (c) 2014 Vesa Kivimäki <contact/dot/diizy/at/nbl/dot/fi>
 * Copyright (c) 2006-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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
 
#include "CrossoverEQ.h"

#include <array>

#include "ModernDsp.h"
#include "lmms_math.h"
#include "embed.h"
#include "plugin_export.h"

namespace lmms
{


extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT crossovereq_plugin_descriptor =
{
	LMMS_STRINGIFY( PLUGIN_NAME ),
	"Crossover Equalizer",
	QT_TRANSLATE_NOOP( "PluginBrowser", "A 4-band Crossover Equalizer" ),
	"Vesa Kivimäki <contact/dot/diizy/at/nbl/dot/fi>",
	0x0100,
	Plugin::Type::Effect,
	new PixmapLoader("lmms-plugin-logo"),
	nullptr,
	nullptr,
};

}


CrossoverEQEffect::CrossoverEQEffect( Model* parent, const Descriptor::SubPluginFeatures::Key* key ) :
	Effect( &crossovereq_plugin_descriptor, parent, key ),
	m_controls( this ),
	m_sampleRate( Engine::audioEngine()->outputSampleRate() ),
	m_lp1( m_sampleRate ),
	m_lp2( m_sampleRate ),
	m_lp3( m_sampleRate ),
	m_hp2( m_sampleRate ),
	m_hp3( m_sampleRate ),
	m_hp4( m_sampleRate ),
	m_needsUpdate( true )
{
	m_tmp2 = new SampleFrame[Engine::audioEngine()->framesPerPeriod()];
	m_tmp1 = new SampleFrame[Engine::audioEngine()->framesPerPeriod()];
	m_work = new SampleFrame[Engine::audioEngine()->framesPerPeriod()];
}

CrossoverEQEffect::~CrossoverEQEffect()
{
	delete[] m_tmp1;
	delete[] m_tmp2;
	delete[] m_work;
}

void CrossoverEQEffect::sampleRateChanged()
{
	m_sampleRate = Engine::audioEngine()->outputSampleRate();
	m_lp1.setSampleRate( m_sampleRate );
	m_lp2.setSampleRate( m_sampleRate );
	m_lp3.setSampleRate( m_sampleRate );
	m_hp2.setSampleRate( m_sampleRate );
	m_hp3.setSampleRate( m_sampleRate );
	m_hp4.setSampleRate( m_sampleRate );
	m_needsUpdate = true;
}


Effect::ProcessStatus CrossoverEQEffect::processImpl(SampleFrame* buf, const f_cnt_t frames)
{
	// filters update
	if( m_needsUpdate || m_controls.m_xover12.isValueChanged() )
	{
		m_lp1.setLowpass( m_controls.m_xover12.value() );
		m_hp2.setHighpass( m_controls.m_xover12.value() );
	}
	if( m_needsUpdate || m_controls.m_xover23.isValueChanged() )
	{
		m_lp2.setLowpass( m_controls.m_xover23.value() );
		m_hp3.setHighpass( m_controls.m_xover23.value() );
	}
	if( m_needsUpdate || m_controls.m_xover34.isValueChanged() )
	{
		m_lp3.setLowpass( m_controls.m_xover34.value() );
		m_hp4.setHighpass( m_controls.m_xover34.value() );
	}
	
	// gain values update
	if( m_needsUpdate || m_controls.m_gain1.isValueChanged() )
	{
		m_gain1 = dbfsToAmp( m_controls.m_gain1.value() );
	}
	if( m_needsUpdate || m_controls.m_gain2.isValueChanged() )
	{
		m_gain2 = dbfsToAmp( m_controls.m_gain2.value() );
	}
	if( m_needsUpdate || m_controls.m_gain3.isValueChanged() )
	{
		m_gain3 = dbfsToAmp( m_controls.m_gain3.value() );
	}
	if( m_needsUpdate || m_controls.m_gain4.isValueChanged() )
	{
		m_gain4 = dbfsToAmp( m_controls.m_gain4.value() );
	}
	
	// mute values update (the models are "enabled" flags: true = band plays)
	const std::array<bool, 4> enabled = {m_controls.m_mute1.value(), m_controls.m_mute2.value(),
		m_controls.m_mute3.value(), m_controls.m_mute4.value()};
	std::array<bool, 4> solo;
	bool anySolo = false;
	std::array<float, 4> width;
	for (int i = 0; i < 4; ++i)
	{
		solo[i] = m_controls.m_solo[i].value();
		anySolo = anySolo || solo[i];
		width[i] = m_controls.m_width[i].value() * 0.01f;
	}
	const std::array<float, 4> gains = {m_gain1, m_gain2, m_gain3, m_gain4};

	m_needsUpdate = false;

	zeroSampleFrames(m_work, frames);

	for (auto f = std::size_t{0}; f < frames; ++f)
	{
		// Split into four bands; the filters always run so they stay in sync when a band is toggled
		std::array<std::array<float, 2>, 4> band;
		for (int ch = 0; ch < 2; ++ch)
		{
			const float low = m_lp2.update(buf[f][ch], ch);
			const float high = m_hp3.update(buf[f][ch], ch);
			band[0][ch] = m_lp1.update(low, ch);
			band[1][ch] = m_hp2.update(low, ch);
			band[2][ch] = m_lp3.update(high, ch);
			band[3][ch] = m_hp4.update(high, ch);
		}

		for (int i = 0; i < 4; ++i)
		{
			// Soloed bands play regardless of their mute state; with any solo, all others are silent
			const bool plays = anySolo ? solo[i] : enabled[i];
			if (!plays) { continue; }
			float left = band[i][0] * gains[i];
			float right = band[i][1] * gains[i];
			if (width[i] != 1.f) { dsp::applyWidth(left, right, width[i]); }
			m_work[f][0] += left;
			m_work[f][1] += right;
		}
	}

	const float d = dryLevel();
	const float w = wetLevel();

	for (auto f = std::size_t{0}; f < frames; ++f)
	{
		buf[f][0] = d * buf[f][0] + w * m_work[f][0];
		buf[f][1] = d * buf[f][1] + w * m_work[f][1];
	}

	return ProcessStatus::ContinueIfNotQuiet;
}

void CrossoverEQEffect::clearFilterHistories()
{
	m_lp1.clearHistory();
	m_lp2.clearHistory();
	m_lp3.clearHistory();
	m_hp2.clearHistory();
	m_hp3.clearHistory();
	m_hp4.clearHistory();
}


extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin * lmms_plugin_main( Model* parent, void* data )
{
	return new CrossoverEQEffect( parent, static_cast<const Plugin::Descriptor::SubPluginFeatures::Key *>( data ) );
}

}


} // namespace lmms
