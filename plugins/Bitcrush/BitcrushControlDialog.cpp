/*
 * BitcrushControlDialog.cpp - A native bitcrusher
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


#include <QLabel>

#include "embed.h"
#include "BitcrushControlDialog.h"

#include <QHBoxLayout>

#include "BitcrushControls.h"
#include "Knob.h"
#include "ModernWidgets.h"

namespace lmms::gui
{


BitcrushControlDialog::BitcrushControlDialog( BitcrushControls * controls ) :
	EffectControlDialog( controls )
{
	modern::applyWindowStyle(this);

	auto layout = new QHBoxLayout(this);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(6);
	layout->setSizeConstraint(QLayout::SetFixedSize);

	auto input = new ModernSection(tr("Input"), this);
	input->grid()->addWidget(modern::makeKnob(this, &controls->m_inGain, tr("GAIN"), tr("Input gain:"), " dBFS"), 0, 0);
	input->grid()->addWidget(modern::makeKnob(this, &controls->m_inNoise, tr("NOISE"), tr("Input noise:"), "%"), 1, 0);
	layout->addWidget(input);

	auto rateSection = new ModernSection(tr("Rate"), this);
	auto rateEnabled = new ModernToggle(tr("ON"), this);
	rateEnabled->setModel(&controls->m_rateEnabled);
	rateEnabled->setToolTip(tr("Enable sample-rate crushing"));
	rateSection->grid()->addWidget(rateEnabled, 0, 0, 1, 2);
	rateSection->grid()->addWidget(modern::makeKnob(this, &controls->m_rate, tr("FREQ"), tr("Sample rate:"), " Hz"), 1, 0);
	rateSection->grid()->addWidget(modern::makeKnob(this, &controls->m_stereoDiff, tr("STEREO"), tr("Stereo difference:"), "%"), 1, 1);
	layout->addWidget(rateSection);

	auto depthSection = new ModernSection(tr("Depth"), this);
	auto depthEnabled = new ModernToggle(tr("ON"), this);
	depthEnabled->setModel(&controls->m_depthEnabled);
	depthEnabled->setToolTip(tr("Enable bit-depth crushing"));
	depthSection->grid()->addWidget(depthEnabled, 0, 0);
	depthSection->grid()->addWidget(modern::makeKnob(this, &controls->m_levels, tr("LEVELS"), tr("Levels:"), ""), 1, 0);
	layout->addWidget(depthSection);

	auto output = new ModernSection(tr("Output"), this);
	output->grid()->addWidget(modern::makeKnob(this, &controls->m_outGain, tr("GAIN"), tr("Output gain:"), " dBFS"), 0, 0);
	output->grid()->addWidget(modern::makeKnob(this, &controls->m_outClip, tr("CLIP"), tr("Output clip:"), " dBFS"), 1, 0);
	layout->addWidget(output);
}


} // namespace lmms::gui
