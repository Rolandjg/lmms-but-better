/*
 * LoudnessMeterDialog.h - display of the loudness meter
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

#ifndef LMMS_GUI_LOUDNESS_METER_DIALOG_H
#define LMMS_GUI_LOUDNESS_METER_DIALOG_H

#include "EffectControlDialog.h"

namespace lmms
{

class LoudnessMeterControls;

namespace gui
{

class LoudnessMeterDialog : public EffectControlDialog
{
	Q_OBJECT
public:
	LoudnessMeterDialog(LoudnessMeterControls* controls);
	~LoudnessMeterDialog() override = default;
};


//! Loudness history graph with the target line, a momentary bar and the numeric readouts
class LoudnessDisplay : public QWidget
{
	Q_OBJECT
public:
	LoudnessDisplay(LoudnessMeterControls* controls, QWidget* parent);
	QSize sizeHint() const override { return QSize(520, 280); }

protected:
	void paintEvent(QPaintEvent*) override;
	void mouseDoubleClickEvent(QMouseEvent*) override;

private:
	LoudnessMeterControls* m_controls;
	float m_barDisplay = -70.f;
	float m_peakHold = -70.f;
	int m_peakHoldTicks = 0;
};

} // namespace gui

} // namespace lmms

#endif // LMMS_GUI_LOUDNESS_METER_DIALOG_H
