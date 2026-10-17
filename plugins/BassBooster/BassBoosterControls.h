/*
 * BassBoosterControls.h - controls for bassboosterx -effect
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

#ifndef BASSBOOSTER_CONTROLS_H
#define BASSBOOSTER_CONTROLS_H

#include "EffectControls.h"
#include "BassBoosterControlDialog.h"

namespace lmms
{

class BassBoosterEffect;


class BassBoosterControls : public EffectControls
{
	Q_OBJECT
public:
	BassBoosterControls( BassBoosterEffect* effect );
	~BassBoosterControls() override = default;

	void saveSettings( QDomDocument & _doc, QDomElement & _parent ) override;
	void loadSettings( const QDomElement & _this ) override;
	inline QString nodeName() const override
	{
		return "bassboostercontrols";
	}

	int controlCount() override
	{
		return 3;
	}

	gui::EffectControlDialog* createView() override
	{
		return new gui::BassBoosterControlDialog( this );
	}


	//! Corner frequency of the boost filter for the legacy "freq" coefficient (which was
	//! independent of the sample rate, so this holds at any rate)
	static float cutoffForCoefficient(float coefficient);
	//! The one-pole coefficient that puts the -3 dB point of the boost filter at @p cutoff
	static float coefficientForCutoff(float cutoff, float sampleRate);

private slots:
	void changeFrequency();

private:
	BassBoosterEffect* m_effect;
	//! Corner frequency in Hz. Projects before it existed stored the raw filter coefficient as
	//! "freq" (where a higher value meant a lower frequency); that is converted on load.
	FloatModel m_freqModel;
	FloatModel m_gainModel;
	FloatModel m_ratioModel;

	//! Output peaks of the last period, written by the audio thread for the meter
	float m_outPeakL = 0.f;
	float m_outPeakR = 0.f;

	friend class gui::BassBoosterControlDialog;
	friend class gui::BassBoosterDisplay;
	friend class BassBoosterEffect;
} ;


} // namespace lmms

#endif
