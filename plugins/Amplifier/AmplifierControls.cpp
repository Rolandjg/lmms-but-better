/*
 * AmplifierControls.cpp - controls for amplifier effect
 *
 * Copyright (c) 2014 Vesa Kivimäki <contact/dot/diizy/at/nbl/dot/fi>
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


#include "AmplifierControls.h"
#include "Amplifier.h"

namespace lmms
{

AmplifierControls::AmplifierControls(AmplifierEffect* effect) :
	EffectControls(effect),
	m_effect(effect),
	m_volumeModel(100.0f, 0.0f, 200.0f, 0.1f, this, tr("Volume")),
	m_panModel(0.0f, -100.0f, 100.0f, 0.1f, this, tr("Panning")),
	m_leftModel(100.0f, 0.0f, 200.0f, 0.1f, this, tr("Left gain")),
	m_rightModel(100.0f, 0.0f, 200.0f, 0.1f, this, tr("Right gain")),
	m_channelModeModel(this, tr("Channels")),
	m_invertLeftModel(false, this, tr("Invert left phase")),
	m_invertRightModel(false, this, tr("Invert right phase")),
	m_widthModel(100.f, 0.f, 400.f, 0.1f, this, tr("Stereo width")),
	m_bassMonoModel(false, this, tr("Bass mono")),
	m_bassMonoFreqModel(120.f, 30.f, 500.f, 1.f, this, tr("Bass mono frequency")),
	m_dcFilterModel(false, this, tr("DC filter"))
{
	m_channelModeModel.addItem(tr("Stereo"));
	m_channelModeModel.addItem(tr("Left"));
	m_channelModeModel.addItem(tr("Right"));
	m_channelModeModel.addItem(tr("Swap"));
	m_bassMonoFreqModel.setScaleLogarithmic(true);
	m_widthModel.setCenterValue(100.f);
}


void AmplifierControls::loadSettings(const QDomElement& parent)
{
	m_volumeModel.loadSettings(parent, "volume");
	m_panModel.loadSettings(parent, "pan");
	m_leftModel.loadSettings(parent, "left");
	m_rightModel.loadSettings(parent, "right");

	// Utility features added later; older projects keep the neutral defaults
	m_channelModeModel.loadSettings(parent, "channels");
	m_invertLeftModel.loadSettings(parent, "invertL");
	m_invertRightModel.loadSettings(parent, "invertR");
	m_widthModel.loadSettings(parent, "width");
	m_bassMonoModel.loadSettings(parent, "bassMono");
	m_bassMonoFreqModel.loadSettings(parent, "bassMonoFreq");
	m_dcFilterModel.loadSettings(parent, "dcFilter");
	m_bassMonoFreqModel.setScaleLogarithmic(true);
}


void AmplifierControls::saveSettings(QDomDocument& doc, QDomElement& parent)
{
	m_volumeModel.saveSettings(doc, parent, "volume"); 
	m_panModel.saveSettings(doc, parent, "pan");
	m_leftModel.saveSettings(doc, parent, "left");
	m_rightModel.saveSettings(doc, parent, "right");
	m_channelModeModel.saveSettings(doc, parent, "channels");
	m_invertLeftModel.saveSettings(doc, parent, "invertL");
	m_invertRightModel.saveSettings(doc, parent, "invertR");
	m_widthModel.saveSettings(doc, parent, "width");
	m_bassMonoModel.saveSettings(doc, parent, "bassMono");
	m_bassMonoFreqModel.saveSettings(doc, parent, "bassMonoFreq");
	m_dcFilterModel.saveSettings(doc, parent, "dcFilter");
}


} // namespace lmms
