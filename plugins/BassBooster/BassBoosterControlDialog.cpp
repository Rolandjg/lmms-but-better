/*
 * BassBoosterControlDialog.cpp - control dialog for bassbooster effect
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


#include <QHBoxLayout>
#include <QVBoxLayout>

#include "BassBoosterControlDialog.h"

#include <QHBoxLayout>

#include "BassBoosterControls.h"
#include "Knob.h"
#include "ModernWidgets.h"


namespace lmms::gui
{


BassBoosterControlDialog::BassBoosterControlDialog( BassBoosterControls* controls ) :
	EffectControlDialog( controls )
{
	modern::applyWindowStyle(this);

	auto layout = new QHBoxLayout(this);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSizeConstraint(QLayout::SetFixedSize);

	auto section = new ModernSection(tr("Bass boost"), this);
	section->grid()->addWidget(modern::makeKnob(this, &controls->m_freqModel, tr("FREQ"), tr("Frequency:"), " Hz"), 0, 0);
	section->grid()->addWidget(modern::makeKnob(this, &controls->m_gainModel, tr("GAIN"), tr("Gain:"), ""), 0, 1);
	section->grid()->addWidget(modern::makeKnob(this, &controls->m_ratioModel, tr("RATIO"), tr("Ratio:"), ""), 0, 2);
	layout->addWidget(section);
}


} // namespace lmms::gui
