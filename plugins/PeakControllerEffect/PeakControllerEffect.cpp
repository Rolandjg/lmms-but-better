/*
 * PeakControllerEffect.cpp - PeakController effect plugin
 *
 * Copyright (c) 2008 Paul Giblock <drfaygo/at/gmail/dot/com>
 * Copyright (c) 2009 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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


#include <cmath>

#include "AudioEngine.h"
#include "Engine.h"
#include "Song.h"
#include "PresetPreviewPlayHandle.h"
#include "PeakController.h"
#include "PeakControllerEffect.h"
#include "lmms_math.h"

#include "embed.h"
#include "plugin_export.h"

namespace lmms
{


extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT peakcontrollereffect_plugin_descriptor =
{
	LMMS_STRINGIFY( PLUGIN_NAME ),
	"Peak Controller",
	QT_TRANSLATE_NOOP( "PluginBrowser",
			"Plugin for controlling knobs with sound peaks" ),
	"Paul Giblock <drfaygo/at/gmail.com>",
	0x0100,
	Plugin::Type::Effect,
	new PixmapLoader("lmms-plugin-logo"),
	nullptr,
	nullptr,
} ;

}

// We have to keep a list of all the PeakController effects so that we can save
// an peakEffect-ID to the project.  This ID is referenced in the PeakController
// settings and is used to set the PeakControllerEffect pointer upon load

//QVector<PeakControllerEffect *> PeakControllerEffect::s_effects;

PeakControllerEffect::PeakControllerEffect(
			Model * _parent,
			const Descriptor::SubPluginFeatures::Key * _key ) :
	Effect( &peakcontrollereffect_plugin_descriptor, _parent, _key ),
	m_effectId(fastRand()),
	m_peakControls( this ),
	m_lastSample( 0 ),
	m_autoController( nullptr )
{
	m_autoController = new PeakController( Engine::getSong(), this );
	if( !Engine::getSong()->isLoadingProject() && !PresetPreviewPlayHandle::isPreviewing() )
	{
		Engine::getSong()->addController( m_autoController );
	}
	PeakController::s_effects.push_back(this);
}




PeakControllerEffect::~PeakControllerEffect()
{
	auto it = std::find(PeakController::s_effects.begin(), PeakController::s_effects.end(), this);
	if (it != PeakController::s_effects.end())
	{
		PeakController::s_effects.erase(it);
		Engine::getSong()->removeController(m_autoController);
	}
}


Effect::ProcessStatus PeakControllerEffect::processImpl(SampleFrame* buf, const f_cnt_t frames)
{
	PeakControllerEffectControls & c = m_peakControls;

	// RMS:
	double sum = 0;

	if( c.m_absModel.value() )
	{
		for (auto i = std::size_t{0}; i < frames; ++i)
		{
			// absolute value is achieved because the squares are > 0
			sum += buf[i][0] * buf[i][0] + buf[i][1] * buf[i][1];
		}
	}
	else
	{
		for (auto i = std::size_t{0}; i < frames; ++i)
		{
			// the value is absolute because of squaring,
			// so we need to correct it
			sum += buf[i][0] * buf[i][0] * sign(buf[i][0])
				+ buf[i][1] * buf[i][1] * sign(buf[i][1]);
		}
	}

	// TODO: flipping this might cause clipping
	// this will mute the output after the values were measured
	if( c.m_muteModel.value() )
	{
		for (auto i = std::size_t{0}; i < frames; ++i)
		{
			buf[i][0] = buf[i][1] = 0.0f;
		}
	}

	float curRMS = sqrt_neg(sum / frames);
	const float measured = curRMS;
	const float tres = c.m_tresholdModel.value();
	const float amount = c.m_amountModel.value() * c.m_amountMultModel.value();
	curRMS = qAbs( curRMS ) < tres ? 0.0f : curRMS;
	m_lastSample = qBound( 0.0f, c.m_baseModel.value() + amount * curRMS, 1.0f );

	// Follow the target like PeakController does sample by sample, so the display shows the
	// controller's real motion even when nothing is connected to it yet
	const float ratio = 44100.0f / Engine::audioEngine()->outputSampleRate();
	const float knob = m_lastSample > m_smoothedSample ? c.m_attackModel.value() : c.m_decayModel.value();
	const float coeff = 1.0f - std::pow(2.0f, -0.3f * (1.0f - knob) * ratio);
	m_smoothedSample += (m_lastSample - m_smoothedSample) * (1.0f - std::pow(1.0f - coeff, static_cast<float>(frames)));

	const auto pos = m_historyPos.load(std::memory_order_relaxed);
	m_inputHistory[pos] = std::abs(measured);
	m_outputHistory[pos] = m_smoothedSample;
	m_historyPos.store((pos + 1) % HistorySize, std::memory_order_relaxed);

	return ProcessStatus::Continue;
}




float PeakControllerEffect::smoothingTimeMs(float knobValue)
{
	// PeakController moves by coeff * distance per sample (at 44.1 kHz), so the time constant is
	// -1 / ln(1 - coeff) samples
	const double coeff = 1.0 - std::pow(2.0, -0.3 * (1.0 - knobValue));
	return static_cast<float>(-1.0 / std::log(1.0 - coeff) / 44.1);
}




extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin * lmms_plugin_main( Model * _parent, void * _data )
{
	return new PeakControllerEffect( _parent,
		static_cast<const Plugin::Descriptor::SubPluginFeatures::Key *>( _data ) );
}

}


} // namespace lmms
