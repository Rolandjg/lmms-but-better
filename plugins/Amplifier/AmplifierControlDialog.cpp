/*
 * AmplifierControlDialog.cpp - control dialog for amplifier effect
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

#include "AmplifierControlDialog.h"

#include <QGridLayout>
#include <QPainter>
#include <QVBoxLayout>

#include "Amplifier.h"
#include "AmplifierControls.h"
#include "Knob.h"
#include "ModernWidgets.h"


namespace lmms::gui
{

AmplifierControlDialog::AmplifierControlDialog(AmplifierControls* controls) :
	EffectControlDialog(controls)
{
	modern::applyWindowStyle(this);

	auto grid = new QGridLayout(this);
	grid->setContentsMargins(8, 8, 8, 8);
	grid->setSpacing(6);
	grid->setSizeConstraint(QLayout::SetFixedSize);


	auto makeVolumeKnob = [this](const QString& label, const QString& hint, FloatModel* model)
	{
		auto knob = new VolumeKnob(KnobType::Modern, label, this);
		knob->setModel(model);
		knob->setHintText(hint, "%");
		return knob;
	};

	auto scope = new ModernStereoScope(this, &controls->m_effect->m_scope);
	grid->addWidget(scope, 0, 0, 2, 1);

	auto gain = new ModernSection(tr("Gain"), this);
	gain->grid()->addWidget(makeVolumeKnob(tr("VOL"), tr("Volume:"), &controls->m_volumeModel), 0, 0);
	gain->grid()->addWidget(modern::makeKnob(this, &controls->m_panModel, tr("PAN"), tr("Panning:"), "%"), 0, 1);
	gain->grid()->addWidget(makeVolumeKnob(tr("LEFT"), tr("Left gain:"), &controls->m_leftModel), 0, 2);
	gain->grid()->addWidget(makeVolumeKnob(tr("RIGHT"), tr("Right gain:"), &controls->m_rightModel), 0, 3);
	grid->addWidget(gain, 0, 1);

	auto stereo = new ModernSection(tr("Stereo"), this);
	auto channels = new ModernSegmented(this);
	channels->setModel(&controls->m_channelModeModel);
	stereo->grid()->addWidget(channels, 0, 0, 1, 2);
	auto invL = new ModernToggle(tr("Ø L"), this);
	invL->setModel(&controls->m_invertLeftModel);
	invL->setToolTip(tr("Invert the phase of the left channel"));
	auto invR = new ModernToggle(tr("Ø R"), this);
	invR->setModel(&controls->m_invertRightModel);
	invR->setToolTip(tr("Invert the phase of the right channel"));
	stereo->grid()->addWidget(invL, 1, 0);
	stereo->grid()->addWidget(invR, 1, 1);
	stereo->grid()->addWidget(modern::makeKnob(this, &controls->m_widthModel, tr("WIDTH"),
		tr("Stereo width:"), "%"), 0, 2, 2, 1);
	grid->addWidget(stereo, 1, 1);

	auto low = new ModernSection(tr("Low end"), this);
	auto bassMono = new ModernToggle(tr("BASS MONO"), this);
	bassMono->setModel(&controls->m_bassMonoModel);
	bassMono->setToolTip(tr("Collapse everything below the frequency to mono"));
	low->grid()->addWidget(bassMono, 0, 0);
	low->grid()->addWidget(modern::makeKnob(this, &controls->m_bassMonoFreqModel, tr("FREQ"),
		tr("Bass mono below:"), " Hz"), 1, 0, Qt::AlignHCenter);
	auto dc = new ModernToggle(tr("DC FILTER"), this);
	dc->setModel(&controls->m_dcFilterModel);
	dc->setToolTip(tr("Remove DC offset"));
	low->grid()->addWidget(dc, 2, 0);
	grid->addWidget(low, 0, 2, 2, 1);

	grid->addWidget(new ModernMeter(this, &controls->m_outPeakL, &controls->m_outPeakR), 0, 3, 2, 1);
}




} // namespace lmms::gui
