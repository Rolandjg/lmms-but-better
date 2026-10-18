/*
 * InstrumentSoundShapingView.h - view for InstrumentSoundShaping class
 *
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef LMMS_GUI_INSTRUMENT_SOUND_SHAPING_VIEW_H
#define LMMS_GUI_INSTRUMENT_SOUND_SHAPING_VIEW_H

#include <QWidget>

#include "ModelView.h"

class QLabel;
class QStackedWidget;

namespace lmms { class InstrumentSoundShaping; class IntModel; }

namespace lmms::gui
{

class EnvelopeAndLfoView;
class ComboBox;
class Knob;
class ModernHeader;
class ModernSegmented;


class InstrumentSoundShapingView : public QWidget, public ModelView
{
	Q_OBJECT
public:
	InstrumentSoundShapingView( QWidget * _parent );
	~InstrumentSoundShapingView() override;

	void setFunctionsHidden( bool hidden );


private:
	void modelChanged() override;


	InstrumentSoundShaping * m_ss = nullptr;
	IntModel* m_targetModel;
	ModernSegmented* m_targetSelector;
	QStackedWidget* m_targetStack;

	EnvelopeAndLfoView* m_volumeView;
	EnvelopeAndLfoView* m_cutoffView;
	EnvelopeAndLfoView* m_resonanceView;

	// filter-stuff
	QWidget* m_filterRow;
	ModernHeader* m_filterToggle;
	ComboBox * m_filterComboBox;
	Knob * m_filterCutKnob;
	Knob * m_filterResKnob;

	QLabel* m_singleStreamInfoLabel;
};


} // namespace lmms::gui

#endif // LMMS_GUI_INSTRUMENT_SOUND_SHAPING_VIEW_H
