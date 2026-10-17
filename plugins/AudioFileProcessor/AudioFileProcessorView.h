/*
 * AudioFileProcessorView.h - View of the AFP
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


#ifndef LMMS_AUDIO_FILE_PROCESSOR_VIEW_H
#define LMMS_AUDIO_FILE_PROCESSOR_VIEW_H

#include "InstrumentView.h"

class QLabel;
class QStackedWidget;

namespace lmms
{

namespace gui
{

class ComboBox;
class Knob;
class LcdSpinBox;
class ModernSegmented;
class ModernToggle;
class AudioFileProcessorWaveView;


class AudioFileProcessorView : public gui::InstrumentView
{
	Q_OBJECT
public:
	AudioFileProcessorView(Instrument* instrument, QWidget* parent);
	~AudioFileProcessorView() override = default;

	QSize sizeHint() const override { return QSize(560, 342); }
	QSize minimumSizeHint() const override { return sizeHint(); }

protected slots:
	void sampleUpdated();
	void openAudioFile();

protected:
	void dragEnterEvent(QDragEnterEvent* dee) override;
	void dropEvent(QDropEvent* de) override;
	void paintEvent(QPaintEvent*) override;
	void mousePressEvent(QMouseEvent* me) override;

private:
	void modelChanged() override;
	void updateModeWidgets();
	QRect fileNameRect() const;

	AudioFileProcessorWaveView* m_waveView;

	ModernSegmented* m_modeSelector;
	ModernToggle* m_reverseButton;
	ModernToggle* m_stutterButton;
	ModernToggle* m_oneShotButton;
	ModernToggle* m_chokeButton;
	ModernToggle* m_snapButton;
	ComboBox* m_interpBox;

	Knob* m_ampKnob;
	QStackedWidget* m_regionStack;
	Knob* m_startKnob;
	Knob* m_loopKnob;
	Knob* m_endKnob;
	Knob* m_fadeInKnob;
	Knob* m_fadeOutKnob;
	ModernSegmented* m_sliceMethod;
	QStackedWidget* m_sliceParamStack;
	Knob* m_sensitivityKnob;
	ModernSegmented* m_divisionSelector;
	QLabel* m_sliceCount;

	ModernSegmented* m_loopSelector;
	Knob* m_crossfadeKnob;
	Knob* m_transposeKnob;
	Knob* m_tuneKnob;
	QWidget* m_detectButton;
	ModernToggle* m_warpButton;
	LcdSpinBox* m_tempoSpinBox;

	QString m_status;
	QList<QWidget*> m_columns;
} ;

} // namespace gui

} // namespace lmms

#endif // LMMS_AUDIO_FILE_PROCESSOR_VIEW_H
