/*
 * eqeffect.cpp - defination of EqEffect class.
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

#include "EqEffect.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

#include "AudioEngine.h"
#include "Engine.h"
#include "lmms_math.h"

#include "embed.h"
#include "plugin_export.h"

namespace lmms
{


extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT eq_plugin_descriptor =
{
	LMMS_STRINGIFY( PLUGIN_NAME ),
	"Equalizer",
	QT_TRANSLATE_NOOP( "PluginBrowser", "A native eq plugin" ),
	"Dave French <contact/dot/dave/dot/french3/at/googlemail/dot/com>",
	0x0100,
	Plugin::Type::Effect,
	new PixmapLoader("lmms-plugin-logo"),
	nullptr,
	nullptr,
} ;

}


EqEffect::EqEffect( Model *parent, const Plugin::Descriptor::SubPluginFeatures::Key *key) :
	Effect( &eq_plugin_descriptor, parent, key ),
	m_eqControls( this ),
	m_inGain( 1.0 ),
	m_outGain( 1.0 )
{
}




Effect::ProcessStatus EqEffect::processImpl(SampleFrame* buf, const f_cnt_t frames)
{
	const int sampleRate = Engine::audioEngine()->outputSampleRate();

	//wet/dry controls
	const float dry = dryLevel();
	const float wet = wetLevel();
	// setup sample exact controls
	float hpRes = m_eqControls.m_hpResModel.value();
	float lowShelfRes = m_eqControls.m_lowShelfResModel.value();
	float para1Bw = m_eqControls.m_para1BwModel.value();
	float para2Bw = m_eqControls.m_para2BwModel.value();
	float para3Bw = m_eqControls.m_para3BwModel.value();
	float para4Bw = m_eqControls.m_para4BwModel.value();
	float highShelfRes = m_eqControls.m_highShelfResModel.value();
	float lpRes = m_eqControls.m_lpResModel.value();

	float hpFreq = m_eqControls.m_hpFeqModel.value();
	float lowShelfFreq = m_eqControls.m_lowShelfFreqModel.value();
	float para1Freq = m_eqControls.m_para1FreqModel.value();
	float para2Freq = m_eqControls.m_para2FreqModel.value();
	float para3Freq = m_eqControls.m_para3FreqModel.value();
	float para4Freq = m_eqControls.m_para4FreqModel.value();
	float highShelfFreq = m_eqControls.m_highShelfFreqModel.value();
	float lpFreq = m_eqControls.m_lpFreqModel.value();


	bool hpActive = m_eqControls.m_hpActiveModel.value();
	bool hp24Active = m_eqControls.m_hp24Model.value();
	bool hp48Active = m_eqControls.m_hp48Model.value();
	bool lowShelfActive = m_eqControls.m_lowShelfActiveModel.value();
	bool para1Active = m_eqControls.m_para1ActiveModel.value();
	bool para2Active = m_eqControls.m_para2ActiveModel.value();
	bool para3Active = m_eqControls.m_para3ActiveModel.value();
	bool para4Active = m_eqControls.m_para4ActiveModel.value();
	bool highShelfActive = m_eqControls.m_highShelfActiveModel.value();
	bool lpActive = m_eqControls.m_lpActiveModel.value();
	bool lp24Active = m_eqControls.m_lp24Model.value();
	bool lp48Active = m_eqControls.m_lp48Model.value();

	float lowShelfGain = m_eqControls.m_lowShelfGainModel.value();
	float para1Gain = m_eqControls.m_para1GainModel.value();
	float para2Gain = m_eqControls.m_para2GainModel.value();
	float para3Gain = m_eqControls.m_para3GainModel.value();
	float para4Gain = m_eqControls.m_para4GainModel.value();
	float highShelfGain = m_eqControls.m_highShelfGainModel.value();

	//set all filter parameters once per frame, EqFilter handles
	//smooth xfading, reducing pops clicks and dc bias offsets

	m_hp12.setParameters( sampleRate, hpFreq, hpRes, 1 );
	m_hp24.setParameters( sampleRate, hpFreq, hpRes, 1 );
	m_hp480.setParameters( sampleRate, hpFreq, hpRes, 1 );
	m_hp481.setParameters( sampleRate, hpFreq, hpRes, 1 );
	m_lowShelf.setParameters( sampleRate, lowShelfFreq, lowShelfRes, lowShelfGain );
	m_para1.setParameters( sampleRate, para1Freq, para1Bw, para1Gain );
	m_para2.setParameters( sampleRate, para2Freq, para2Bw, para2Gain );
	m_para3.setParameters( sampleRate, para3Freq, para3Bw, para3Gain );
	m_para4.setParameters( sampleRate, para4Freq, para4Bw, para4Gain );
	m_highShelf.setParameters( sampleRate, highShelfFreq, highShelfRes, highShelfGain );
	m_lp12.setParameters( sampleRate, lpFreq, lpRes, 1 );
	m_lp24.setParameters( sampleRate, lpFreq, lpRes, 1 );
	m_lp480.setParameters( sampleRate, lpFreq, lpRes, 1 );
	m_lp481.setParameters( sampleRate, lpFreq, lpRes, 1 );


	if( m_eqControls.m_outGainModel.isValueChanged() )
	{
		m_outGain = dbfsToAmp(m_eqControls.m_outGainModel.value());
	}

	if( m_eqControls.m_inGainModel.isValueChanged() )
	{
		m_inGain = dbfsToAmp(m_eqControls.m_inGainModel.value());
	}

	// the analysers and meters are only shown in the editor
	const bool visible = m_eqControls.isViewVisible();
	const bool analyseIn = m_eqControls.m_analyseInModel.value( true ) && visible;
	const bool analyseOut = m_eqControls.m_analyseOutModel.value( true ) && visible;

	// the input energy only gates the analysers, so skip it when neither runs
	double outSum = 0.0;
	if( analyseIn || analyseOut )
	{
		for (f_cnt_t f = 0; f < frames; ++f)
		{
			outSum += buf[f][0] * buf[f][0] + buf[f][1] * buf[f][1];
		}
	}

	const float outGain =  m_outGain;
	SampleFrame m_inPeak = { 0, 0 };

	if( analyseIn && outSum > 0 )
	{
		m_eqControls.m_inFftBands.analyze( buf, frames );
	}
	else
	{
		m_eqControls.m_inFftBands.clear();
	}

	gain( buf, frames, m_inGain, visible ? &m_inPeak : nullptr );
	m_eqControls.m_inPeakL = m_eqControls.m_inPeakL < m_inPeak[0] ? m_inPeak[0] : m_eqControls.m_inPeakL;
	m_eqControls.m_inPeakR = m_eqControls.m_inPeakR < m_inPeak[1] ? m_inPeak[1] : m_eqControls.m_inPeakR;

	// Channel mode: the bands process L/R (0), only the mid (1) or only the side (2) signal
	const int channelMode = m_eqControls.m_channelModeModel.value();

	// Solo: replace the output with the region the band acts on, so it can be heard in isolation
	const int solo = m_eqControls.m_soloBandModel.value();
	if (solo != m_lastSolo)
	{
		for (auto& filter : m_solo) { filter.reset(); }
		m_lastSolo = solo;
	}
	if (solo >= 0)
	{
		using Type = dsp::Biquad::Type;
		// Octave bandwidth to Q for the peak bands
		auto bwToQ = [](float bw) { return 1.f / (2.f * std::sinh(std::numbers::ln2_v<float> / 2.f * std::max(bw, 0.05f))); };
		Type type = Type::Bandpass;
		float freq = 1000.f;
		float q = 0.707f;
		switch (solo)
		{
		case 0: type = Type::Lowpass; freq = hpFreq; break;          // what the high-pass removes
		case 1: type = Type::Lowpass; freq = lowShelfFreq; break;
		case 2: freq = para1Freq; q = bwToQ(para1Bw); break;
		case 3: freq = para2Freq; q = bwToQ(para2Bw); break;
		case 4: freq = para3Freq; q = bwToQ(para3Bw); break;
		case 5: freq = para4Freq; q = bwToQ(para4Bw); break;
		case 6: type = Type::Highpass; freq = highShelfFreq; break;
		default: type = Type::Highpass; freq = lpFreq; break;        // what the low-pass removes
		}
		for (auto& filter : m_solo) { filter.set(type, freq, q, sampleRate); }
	}

	// the filter sections in signal order, and whether each one runs; a soloed band replaces all of them
	const bool bands = solo < 0;
	const std::array<std::pair<EqFilter*, bool>, 14> sections = {{
		{ &m_hp12, bands && hpActive },
		{ &m_hp24, bands && hpActive && ( hp24Active || hp48Active ) },
		{ &m_hp480, bands && hpActive && hp48Active },
		{ &m_hp481, bands && hpActive && hp48Active },
		{ &m_lowShelf, bands && lowShelfActive },
		{ &m_para1, bands && para1Active },
		{ &m_para2, bands && para2Active },
		{ &m_para3, bands && para3Active },
		{ &m_para4, bands && para4Active },
		{ &m_highShelf, bands && highShelfActive },
		{ &m_lp12, bands && lpActive },
		{ &m_lp24, bands && lpActive && ( lp24Active || lp48Active ) },
		{ &m_lp480, bands && lpActive && lp48Active },
		{ &m_lp481, bands && lpActive && lp48Active },
	}};
	std::array<EqFilter*, EqFilter::MaxChainLength> chain;
	int chainLength = 0;
	for( const auto& [filter, active] : sections )
	{
		if( active ) { chain[chainLength++] = filter; }
		else { filter->stop(); }
	}

	const bool midSide = bands && channelMode != 0;
	// the unprocessed signal is needed for the wet/dry mix and mid/side decoding
	const bool needDry = dry != 0.f || midSide;

	// in chunks, so the unprocessed signal always fits m_dryBuf
	for( f_cnt_t offset = 0; offset < frames; offset += m_dryBuf.size() )
	{
		SampleFrame* chunk = buf + offset;
		const f_cnt_t chunkFrames = std::min<f_cnt_t>( frames - offset, m_dryBuf.size() );
		if( needDry ) { std::copy( chunk, chunk + chunkFrames, m_dryBuf.begin() ); }

		if( !bands )
		{
			for( f_cnt_t f = 0; f < chunkFrames; ++f )
			{
				chunk[f][0] = m_solo[0].process( chunk[f][0] );
				chunk[f][1] = m_solo[1].process( chunk[f][1] );
			}
		}
		else
		{
			if( midSide )
			{
				// Process mid in channel 0 and side in channel 1
				for( f_cnt_t f = 0; f < chunkFrames; ++f )
				{
					const float l = chunk[f][0];
					const float r = chunk[f][1];
					chunk[f][0] = (l + r) * 0.5f;
					chunk[f][1] = (l - r) * 0.5f;
				}
			}

			EqFilter::processChain( chain.data(), chainLength, chunk, chunkFrames );

			if( midSide )
			{
				// Keep the unprocessed half and decode back to left/right
				for( f_cnt_t f = 0; f < chunkFrames; ++f )
				{
					const auto& input = m_dryBuf[f];
					const float mid = channelMode == 1 ? chunk[f][0] : (input[0] + input[1]) * 0.5f;
					const float side = channelMode == 2 ? chunk[f][1] : (input[0] - input[1]) * 0.5f;
					chunk[f][0] = mid + side;
					chunk[f][1] = mid - side;
				}
			}
		}

		//apply wet / dry levels
		if( dry != 0.f )
		{
			for( f_cnt_t f = 0; f < chunkFrames; ++f )
			{
				chunk[f][0] = ( dry * m_dryBuf[f][0] ) + ( wet * chunk[f][0] );
				chunk[f][1] = ( dry * m_dryBuf[f][1] ) + ( wet * chunk[f][1] );
			}
		}
		else if( wet != 1.f )
		{
			for( f_cnt_t f = 0; f < chunkFrames; ++f )
			{
				chunk[f] *= wet;
			}
		}
	}

	SampleFrame outPeak = { 0, 0 };
	gain( buf, frames, outGain, visible ? &outPeak : nullptr );
	m_eqControls.m_outPeakL = m_eqControls.m_outPeakL < outPeak[0] ? outPeak[0] : m_eqControls.m_outPeakL;
	m_eqControls.m_outPeakR = m_eqControls.m_outPeakR < outPeak[1] ? outPeak[1] : m_eqControls.m_outPeakR;

	if( analyseOut && outSum > 0 )
	{
		m_eqControls.m_outFftBands.analyze( buf, frames );
	}
	else
	{
		m_eqControls.m_outFftBands.clear();
	}

	return Effect::ProcessStatus::ContinueIfNotQuiet;
}




extern "C"
{

//needed for getting plugin out of shared lib
PLUGIN_EXPORT Plugin * lmms_plugin_main( Model* parent, void* data )
{
	return new EqEffect( parent , static_cast<const Plugin::Descriptor::SubPluginFeatures::Key *>( data ) );
}

}


} // namespace lmms
