/*
 * DelayControls.cpp - definition of DelayControls class.
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


#include "DelayControls.h"
#include "DelayEffect.h"
#include "Engine.h"

namespace lmms
{

DelayControls::DelayControls( DelayEffect* effect ):
	EffectControls( effect ),
	m_effect ( effect ),
	m_delayTimeModel(0.5f, 0.01f, 5.f, 0.0001f, 5000.f, this, tr("Delay time")) ,
	m_feedbackModel(0.0f, 0.0f, 1.0f, 0.01f, this, tr("Feedback")),
	m_lfoTimeModel(2.f, 0.01f, 5.f, 0.0001f, 20000.f, this, tr("LFO frequency")),
	m_lfoAmountModel(0.f, 0.f, 0.5f, 0.0001f, 2000.f, this, tr("LFO amount")),
	m_outGainModel(0.f, -60.f, 20.f, 0.01f, this, tr("Output gain")),
	m_modeModel(this, tr("Mode")),
	m_offsetModel(0.f, -50.f, 50.f, 0.1f, this, tr("Stereo offset")),
	m_lowCutModel(20.f, 20.f, 2000.f, 1.f, this, tr("Low cut")),
	m_highCutModel(20000.f, 500.f, 20000.f, 1.f, this, tr("High cut")),
	m_driveModel(0.f, 0.f, 100.f, 0.1f, this, tr("Drive")),
	m_widthModel(100.f, 0.f, 200.f, 0.1f, this, tr("Width")),
	m_duckModel(0.f, 0.f, 100.f, 0.1f, this, tr("Ducking")),
	m_freezeModel(false, this, tr("Freeze"))
{
	m_modeModel.addItem(tr("Stereo"));
	m_modeModel.addItem(tr("Ping-pong"));
	m_modeModel.addItem(tr("Mono"));

	m_lowCutModel.setScaleLogarithmic(true);
	m_highCutModel.setScaleLogarithmic(true);

	connect( Engine::audioEngine(), SIGNAL( sampleRateChanged() ), this, SLOT( changeSampleRate() ) );
	m_outPeakL = 0.0;
	m_outPeakR = 0.0;
}




void DelayControls::loadSettings( const QDomElement &_this )
{
	m_delayTimeModel.loadSettings(_this, "DelayTimeSamples" );
	m_feedbackModel.loadSettings( _this, "FeebackAmount" );
	m_lfoTimeModel.loadSettings( _this , "LfoFrequency");
	m_lfoAmountModel.loadSettings( _this, "LfoAmount");
	m_outGainModel.loadSettings( _this, "OutGain" );

	// Parameters added with the modernized delay. Older projects don't
	// contain them, so they fall back to the neutral defaults.
	m_modeModel.loadSettings(_this, "Mode");
	m_offsetModel.loadSettings(_this, "StereoOffset");
	m_lowCutModel.loadSettings(_this, "LowCut");
	m_highCutModel.loadSettings(_this, "HighCut");
	m_driveModel.loadSettings(_this, "Drive");
	m_widthModel.loadSettings(_this, "Width");
	m_duckModel.loadSettings(_this, "Ducking");
	m_freezeModel.loadSettings(_this, "Freeze");

	// Missing entries reset the scale type to linear, so restore the intended curves
	m_lowCutModel.setScaleLogarithmic(true);
	m_highCutModel.setScaleLogarithmic(true);
}




void DelayControls::saveSettings( QDomDocument& doc, QDomElement& _this )
{
	m_delayTimeModel.saveSettings( doc, _this, "DelayTimeSamples" );
	m_feedbackModel.saveSettings( doc, _this ,"FeebackAmount" );
	m_lfoTimeModel.saveSettings( doc, _this, "LfoFrequency" );
	m_lfoAmountModel.saveSettings( doc, _this ,"LfoAmount" );
	m_outGainModel.saveSettings( doc, _this, "OutGain" );

	m_modeModel.saveSettings(doc, _this, "Mode");
	m_offsetModel.saveSettings(doc, _this, "StereoOffset");
	m_lowCutModel.saveSettings(doc, _this, "LowCut");
	m_highCutModel.saveSettings(doc, _this, "HighCut");
	m_driveModel.saveSettings(doc, _this, "Drive");
	m_widthModel.saveSettings(doc, _this, "Width");
	m_duckModel.saveSettings(doc, _this, "Ducking");
	m_freezeModel.saveSettings(doc, _this, "Freeze");
}



void DelayControls::changeSampleRate()
{
	m_effect->changeSampleRate();
}


} // namespace lmms
