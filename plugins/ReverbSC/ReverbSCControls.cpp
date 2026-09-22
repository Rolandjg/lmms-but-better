/*
 * ReverbSCControls.cpp - controls for ReverbSC
 *
 * Copyright (c) 2017 Paul Batchelor
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



#include "ReverbSCControls.h"
#include "ReverbSC.h"
#include "Engine.h"

namespace lmms
{


ReverbSCControls::ReverbSCControls( ReverbSCEffect* effect ) :
	EffectControls( effect ),
	m_effect( effect ),
	m_inputGainModel( 0.0f, -60.0f, 15, 0.1f, this, tr( "Input gain" ) ),
	m_sizeModel( 0.89f, 0.0f, 1.0f, 0.01f, this, tr( "Size" ) ),
	m_colorModel( 10000.0f, 100.0f, 15000.0f, 0.1f, this, tr( "Color" ) ),
	m_outputGainModel( 0.0f, -60.0f, 15, 0.1f, this, tr( "Output gain" ) ),
	m_predelayModel(0.f, 0.f, 500.f, 0.1f, this, tr("Pre-delay")),
	m_lowCutModel(20.f, 20.f, 1000.f, 1.f, this, tr("Low cut")),
	m_modulationModel(100.f, 0.f, 100.f, 0.1f, this, tr("Modulation")),
	m_widthModel(100.f, 0.f, 200.f, 0.1f, this, tr("Width")),
	m_duckModel(0.f, 0.f, 100.f, 0.1f, this, tr("Ducking")),
	m_freezeModel(false, this, tr("Freeze"))
{
	m_colorModel.setScaleLogarithmic(true);
	m_lowCutModel.setScaleLogarithmic(true);
	connect( Engine::audioEngine(), SIGNAL( sampleRateChanged() ), this, SLOT( changeSampleRate() ));
}

void ReverbSCControls::changeControl()
{
}

void ReverbSCControls::loadSettings( const QDomElement& _this )
{
	m_inputGainModel.loadSettings( _this, "input_gain" );
	m_sizeModel.loadSettings( _this, "size" );
	m_colorModel.loadSettings( _this, "color" );
	m_outputGainModel.loadSettings( _this, "output_gain" );

	// Added with the modernized reverb; absent in older projects, which then use the defaults
	m_predelayModel.loadSettings(_this, "predelay");
	m_lowCutModel.loadSettings(_this, "lowcut");
	m_modulationModel.loadSettings(_this, "modulation");
	m_widthModel.loadSettings(_this, "width");
	m_duckModel.loadSettings(_this, "ducking");
	m_freezeModel.loadSettings(_this, "freeze");

	// Missing entries reset the scale type to linear, so restore the intended curves
	m_colorModel.setScaleLogarithmic(true);
	m_lowCutModel.setScaleLogarithmic(true);
}

void ReverbSCControls::saveSettings( QDomDocument& doc, QDomElement& _this )
{
	m_inputGainModel.saveSettings( doc, _this, "input_gain" ); 
	m_sizeModel.saveSettings( doc, _this, "size" ); 
	m_colorModel.saveSettings( doc, _this, "color" );
	m_outputGainModel.saveSettings( doc, _this, "output_gain" ); 
	m_predelayModel.saveSettings(doc, _this, "predelay");
	m_lowCutModel.saveSettings(doc, _this, "lowcut");
	m_modulationModel.saveSettings(doc, _this, "modulation");
	m_widthModel.saveSettings(doc, _this, "width");
	m_duckModel.saveSettings(doc, _this, "ducking");
	m_freezeModel.saveSettings(doc, _this, "freeze");
}

void ReverbSCControls::changeSampleRate()
{
	m_effect->changeSampleRate();
}


} // namespace lmms
