/*
 * peak_controller_EffectControlDialog.h - control dialog for
 *                                           peakControllerEffect
 *
 * Copyright (c) 2008 Paul Giblock <drfaygo/at/gmail/dot/com>
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

#ifndef _PEAK_CONTROLLER_EFFECT_CONTROL_DIALOG_H
#define _PEAK_CONTROLLER_EFFECT_CONTROL_DIALOG_H

#include "EffectControlDialog.h"

#include <QWidget>

namespace lmms
{

class PeakControllerEffectControls;

namespace gui
{


class PeakControllerEffectControlDialog : public EffectControlDialog
{
	Q_OBJECT
public:
	PeakControllerEffectControlDialog(
				PeakControllerEffectControls * _controls );
	~PeakControllerEffectControlDialog() override = default;
} ;


//! Scrolling history of the measured input level and the resulting controller value, with the
//! threshold (draggable) and base level drawn in, plus the knob values translated into ms and dB
class PeakControllerDisplay : public QWidget
{
	Q_OBJECT
public:
	PeakControllerDisplay(PeakControllerEffectControls* controls, QWidget* parent);
	QSize sizeHint() const override { return QSize(392, 136); }

protected:
	void paintEvent(QPaintEvent*) override;
	void mousePressEvent(QMouseEvent*) override;
	void mouseMoveEvent(QMouseEvent*) override;
	void mouseReleaseEvent(QMouseEvent*) override;

private:
	QRectF plotRect() const;
	double toY(double value) const;
	bool nearThreshold(const QPointF& pos) const;

	PeakControllerEffectControls* m_controls;
	bool m_dragging = false;
};

} // namespace gui

} // namespace lmms

#endif
