/*
 * WaveShaper.cpp - waveshaper effect-plugin
 *
 * Copyright (c) 2014 Vesa Kivimäki <contact/dot/diizy/at/nbl/dot/fi>
 * Copyright (c) 2006-2009 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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


#include "WaveShaper.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "lmms_math.h"
#include "embed.h"

#include "plugin_export.h"

namespace lmms
{


extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT waveshaper_plugin_descriptor =
{
	LMMS_STRINGIFY( PLUGIN_NAME ),
	"Waveshaper Effect",
	QT_TRANSLATE_NOOP( "PluginBrowser",
				"plugin for waveshaping" ),
	"Vesa Kivimäki <contact/dot/diizy/at/nbl/dot/fi>",
	0x0100,
	Plugin::Type::Effect,
	new PixmapLoader("lmms-plugin-logo"),
	nullptr,
	nullptr,
} ;

}



WaveShaperEffect::WaveShaperEffect( Model * _parent,
			const Descriptor::SubPluginFeatures::Key * _key ) :
	Effect( &waveshaper_plugin_descriptor, _parent, _key ),
	m_wsControls( this )
{
}




Effect::ProcessStatus WaveShaperEffect::processImpl(SampleFrame* buf, const f_cnt_t frames)
{
	const float d = dryLevel();
	const float w = wetLevel();
	float input = m_wsControls.m_inputModel.value();
	float output = m_wsControls.m_outputModel.value();
	const float * samples = m_wsControls.m_wavegraphModel.samples();
	const bool clip = m_wsControls.m_clipModel.value();

	ValueBuffer *inputBuffer = m_wsControls.m_inputModel.valueBuffer();
	ValueBuffer *outputBufer = m_wsControls.m_outputModel.valueBuffer();

	int inputInc = inputBuffer ? 1 : 0;
	int outputInc = outputBufer ? 1 : 0;

	const float *inputPtr = inputBuffer ? &( inputBuffer->values()[ 0 ] ) : &input;
	const float *outputPtr = outputBufer ? &( outputBufer->values()[ 0 ] ) : &output;

	const int stages = std::clamp(m_wsControls.m_oversampleModel.value(), 0, MaxOversampleStages);
	const float sampleRate = Engine::audioEngine()->outputSampleRate();
	if (stages != m_stages || sampleRate != m_sampleRate)
	{
		m_stages = stages;
		m_sampleRate = sampleRate;
		if (stages > 0)
		{
			for (auto& up : m_upsamplers) { up.setup(stages, sampleRate); }
			for (auto& down : m_downsamplers) { down.setup(stages, sampleRate); }
		}
	}
	const int factor = 1 << stages;

	// The drawn curve covers |x| in [0, 1] with 200 points and is mirrored for negative input
	auto shape = [samples](float x)
	{
		const float magnitude = std::abs(x) * 200.0f;
		const int lookup = static_cast<int>(magnitude);
		const float frac = fraction(magnitude);
		const float posneg = x < 0 ? -1.0f : 1.0f;
		if (lookup < 1) { return frac * samples[0] * posneg; }
		if (lookup < 200) { return std::lerp(samples[lookup - 1], samples[lookup], frac) * posneg; }
		return x * samples[199];
	};

	float peak = 0.f;
	std::array<float, 1 << MaxOversampleStages> oversampled;

	for (f_cnt_t f = 0; f < frames; ++f)
	{
		auto s = std::array{buf[f][0], buf[f][1]};

// apply input gain
		s[0] *= *inputPtr;
		s[1] *= *inputPtr;

// clip if clip enabled
		if( clip )
		{
			s[0] = qBound( -1.0f, s[0], 1.0f );
			s[1] = qBound( -1.0f, s[1], 1.0f );
		}

		peak = std::max({peak, std::abs(s[0]), std::abs(s[1])});

// start effect
		for (int ch = 0; ch < 2; ++ch)
		{
			if (stages == 0)
			{
				s[ch] = shape(s[ch]);
				continue;
			}
			// Shaping creates harmonics above Nyquist; running it at a higher rate and filtering
			// on the way down keeps them from folding back as inharmonic aliasing
			m_upsamplers[ch].processSample(oversampled.data(), s[ch]);
			for (int k = 0; k < factor; ++k) { oversampled[k] = shape(oversampled[k]); }
			s[ch] = m_downsamplers[ch].processSample(oversampled.data());
		}

// apply output gain
		s[0] *= *outputPtr;
		s[1] *= *outputPtr;

// mix wet/dry signals
		buf[f][0] = d * buf[f][0] + w * s[0];
		buf[f][1] = d * buf[f][1] + w * s[1];

		outputPtr += outputInc;
		inputPtr += inputInc;
	}

	m_inputLevel.store(peak, std::memory_order_relaxed);

	return ProcessStatus::ContinueIfNotQuiet;
}




extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin * lmms_plugin_main( Model * _parent, void * _data )
{
	return( new WaveShaperEffect( _parent,
		static_cast<const Plugin::Descriptor::SubPluginFeatures::Key *>(
								_data ) ) );
}

}


} // namespace lmms
