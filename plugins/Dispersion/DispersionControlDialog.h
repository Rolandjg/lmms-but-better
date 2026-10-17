/*
 * DispersionControlDialog.h
 *
 * Copyright (c) 2023 Lost Robot <r94231/at/gmail/dot/com>
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

#ifndef LMMS_GUI_DISPERSION_CONTROL_DIALOG_H
#define LMMS_GUI_DISPERSION_CONTROL_DIALOG_H

#include "EffectControlDialog.h"

#include <QWidget>
#include <vector>

class QTimer;

namespace lmms
{

class DispersionControls;


namespace gui
{

class DispersionControlDialog : public EffectControlDialog
{
	Q_OBJECT
public:
	DispersionControlDialog(DispersionControls* controls);
	~DispersionControlDialog() override = default;
};


//! What the all-pass chain does: how long each frequency is delayed (the "chirp" shape) and the
//! resulting impulse response, i.e. what a click turns into
class DispersionDisplay : public QWidget
{
	Q_OBJECT
public:
	DispersionDisplay(DispersionControls* controls, QWidget* parent);
	QSize sizeHint() const override { return QSize(360, 170); }

protected:
	void paintEvent(QPaintEvent*) override;

private:
	//! Recompute the cached curves; parameter changes are coalesced through a short timer
	void recompute();

	DispersionControls* m_controls;
	QTimer* m_recomputeTimer;
	std::vector<float> m_delayMs;    //!< Group delay per display column
	std::vector<float> m_impulse;    //!< Impulse response, one value per sample
	float m_maxDelayMs = 0.f;
	float m_peakFrequency = 0.f;
};


} // namespace gui

} // namespace lmms

#endif // LMMS_GUI_DISPERSION_CONTROL_DIALOG_H
