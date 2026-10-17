/*
 * BassBoosterControls.cpp - controls for bassbooster effect
 *
 * Copyright (c) 2008-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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



#include "BassBoosterControls.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include <QDomElement>

#include "BassBooster.h"

namespace lmms
{


BassBoosterControls::BassBoosterControls( BassBoosterEffect* effect ) :
	EffectControls( effect ),
	m_effect( effect ),
	// The default matches the old default coefficient of 100
	m_freqModel( 69.84f, 20.0f, 250.0f, 0.01f, this, tr( "Frequency" ) ),
	m_gainModel( 1.0f, 0.1f, 5.0f, 0.05f, this, tr( "Gain" ) ),
	m_ratioModel( 2.0f, 0.1f, 10.0f, 0.1f, this, tr( "Ratio" ) )
{
	m_freqModel.setScaleLogarithmic(true);
	connect( Engine::audioEngine(), SIGNAL( sampleRateChanged() ), this, SLOT( changeFrequency() ) );
}


void BassBoosterControls::changeFrequency()
{
	m_effect->m_frequencyChangeNeeded = true;
}



float BassBoosterControls::cutoffForCoefficient(float coefficient)
{
	// The filter is y = (x + y1 * c) / (c + 1), a one-pole lowpass with pole a = c / (c + 1).
	// Its -3 dB point satisfies cos(w) = 1 - (1 - a)^2 / (2a). The effect used to scale c with
	// the sample rate relative to 44.1 kHz, so evaluating at 44.1 kHz gives the real cutoff.
	// clamped, so a missing or broken value can't produce NaN
	const double a = std::max(coefficient, 1.f) / (std::max(coefficient, 1.f) + 1.0);
	const double w = std::acos(std::clamp(1.0 - (1.0 - a) * (1.0 - a) / (2.0 * a), -1.0, 1.0));
	return static_cast<float>(w * 44100.0 / (2.0 * std::numbers::pi));
}

float BassBoosterControls::coefficientForCutoff(float cutoff, float sampleRate)
{
	const double c = 1.0 - std::cos(2.0 * std::numbers::pi * cutoff / sampleRate);
	const double oneMinusA = -c + std::sqrt(c * c + 2.0 * c);
	return static_cast<float>((1.0 - oneMinusA) / oneMinusA);
}



void BassBoosterControls::loadSettings( const QDomElement& _this )
{
	if (_this.hasAttribute("cutoff") || !_this.firstChildElement("cutoff").isNull())
	{
		m_freqModel.loadSettings(_this, "cutoff");
	}
	else if (_this.hasAttribute("freq"))
	{
		m_freqModel.setValue(cutoffForCoefficient(_this.attribute("freq").toFloat()));
	}
	else if (const auto legacy = _this.firstChildElement("freq"); !legacy.isNull())
	{
		// an automated or controlled "freq" was saved as a child element
		m_freqModel.setValue(cutoffForCoefficient(legacy.attribute("value").toFloat()));
	}
	m_gainModel.loadSettings( _this, "gain" );
	m_ratioModel.loadSettings( _this, "ratio");
}




void BassBoosterControls::saveSettings( QDomDocument& doc, QDomElement& _this )
{
	m_freqModel.saveSettings(doc, _this, "cutoff");
	// Older versions only know the raw coefficient
	_this.setAttribute("freq", coefficientForCutoff(m_freqModel.value(), 44100.f));
	m_gainModel.saveSettings( doc, _this, "gain" );
	m_ratioModel.saveSettings( doc, _this, "ratio");
}


} // namespace lmms
