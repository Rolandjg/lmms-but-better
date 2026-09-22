/*
 * StereoEnhancer.cpp - stereo-enhancer-effect-plugin
 *
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


#include "StereoEnhancer.h"

#include "AudioEngine.h"
#include "Engine.h"

#include "embed.h"
#include "plugin_export.h"

namespace lmms
{


extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT stereoenhancer_plugin_descriptor =
{
	LMMS_STRINGIFY( PLUGIN_NAME ),
	"StereoEnhancer Effect",
	QT_TRANSLATE_NOOP( "PluginBrowser",
				"Plugin for enhancing stereo separation of a stereo input file" ),
	"Lou Herard <lherard/at/gmail.com>",
	0x0100,
	Plugin::Type::Effect,
	new PixmapLoader("lmms-plugin-logo"),
	nullptr,
	nullptr,
} ;

}



StereoEnhancerEffect::StereoEnhancerEffect(
			Model * _parent,
			const Descriptor::SubPluginFeatures::Key * _key ) :
	Effect( &stereoenhancer_plugin_descriptor, _parent, _key ),
	m_seFX( DspEffectLibrary::StereoEnhancer( 0.0f ) ),
	m_delayBuffer( new SampleFrame[DEFAULT_BUFFER_SIZE] ),
	m_currFrame( 0 ),
	m_bbControls( this )
{
	// TODO:  Make m_delayBuffer customizable?
	clearMyBuffer();
}




StereoEnhancerEffect::~StereoEnhancerEffect()
{
	if( m_delayBuffer )
	{
		delete [] m_delayBuffer;
	}

	m_currFrame = 0;
}




Effect::ProcessStatus StereoEnhancerEffect::processImpl(SampleFrame* buf, const f_cnt_t frames)
{
	m_delayBufferCleared = false;
	const float d = dryLevel();
	const float w = wetLevel();
	const float sr = Engine::audioEngine()->outputSampleRate();

	auto& c = m_bbControls;
	const float lowWidth = c.m_lowWidthModel.value() * 0.01f;
	const float midWidth = c.m_midWidthModel.value() * 0.01f;
	const float highWidth = c.m_highWidthModel.value() * 0.01f;
	const bool multiband = lowWidth != 1.f || midWidth != 1.f || highWidth != 1.f;
	for (auto ch = 0; ch < 2; ++ch)
	{
		m_lowSplit[ch].setCutoff(c.m_lowCrossoverModel.value(), sr);
		m_highSplit[ch].setCutoff(c.m_highCrossoverModel.value(), sr);
	}

	SampleFrame peak;

	for (f_cnt_t f = 0; f < frames; ++f)
	{
		auto in = std::array{buf[f][0], buf[f][1]};

		if (multiband)
		{
			// Complementary three-band split (low + mid + high == input), each band gets its own width
			std::array<float, 2> low, mid, high;
			for (auto ch = 0; ch < 2; ++ch)
			{
				low[ch] = m_lowSplit[ch].lowpass(in[ch]);
				const float rest = in[ch] - low[ch];
				mid[ch] = m_highSplit[ch].lowpass(rest);
				high[ch] = rest - mid[ch];
			}
			dsp::applyWidth(low[0], low[1], lowWidth);
			dsp::applyWidth(mid[0], mid[1], midWidth);
			dsp::applyWidth(high[0], high[1], highWidth);
			in = {low[0] + mid[0] + high[0], low[1] + mid[1] + high[1]};
		}

		// copy samples into the delay buffer
		m_delayBuffer[m_currFrame][0] = in[0];
		m_delayBuffer[m_currFrame][1] = in[1];

		// Get the width knob value from the Stereo Enhancer effect
		float width = m_seFX.wideCoeff();

		// Calculate the correct sample frame for processing
		int frameIndex = m_currFrame - width;

		if( frameIndex < 0 )
		{
			// e.g. difference = -10, frameIndex = DBS - 10
			frameIndex += DEFAULT_BUFFER_SIZE;
		}

		auto s = std::array{in[0], m_delayBuffer[frameIndex][1]};

		m_seFX.nextSample( s[0], s[1] );

		peak = peak.absMax(SampleFrame(s[0], s[1]));
		m_scope.push(s[0], s[1]);

		buf[f][0] = d * buf[f][0] + w * s[0];
		buf[f][1] = d * buf[f][1] + w * s[1];

		// Update currFrame
		m_currFrame += 1;
		m_currFrame %= DEFAULT_BUFFER_SIZE;
	}

	c.m_outPeakL = peak.left();
	c.m_outPeakR = peak.right();

	return ProcessStatus::ContinueIfNotQuiet;
}




void StereoEnhancerEffect::processBypassedImpl()
{
	clearMyBuffer();
}




void StereoEnhancerEffect::clearMyBuffer()
{
	if (m_delayBufferCleared) { return; }

	for (auto i = std::size_t{0}; i < DEFAULT_BUFFER_SIZE; i++)
	{
		m_delayBuffer[i][0] = 0.0f;
		m_delayBuffer[i][1] = 0.0f;
	}

	m_currFrame = 0;
	m_delayBufferCleared = true;
}





extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin * lmms_plugin_main( Model * _parent, void * _data )
{
	return( new StereoEnhancerEffect( _parent,
		static_cast<const Plugin::Descriptor::SubPluginFeatures::Key *>(
								_data ) ) );
}

}


} // namespace lmms
