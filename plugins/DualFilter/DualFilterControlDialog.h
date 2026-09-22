/*
 * DualFilterControlDialog.h - control dialog for dual filter effect
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

#ifndef DUALFILTER_CONTROL_DIALOG_H
#define DUALFILTER_CONTROL_DIALOG_H

#include "EffectControlDialog.h"

namespace lmms
{


class DualFilterControls;

namespace gui
{


class DualFilterControlDialog : public EffectControlDialog
{
	Q_OBJECT
public:
	DualFilterControlDialog( DualFilterControls* controls );
	~DualFilterControlDialog() override = default;

} ;


//! Combined magnitude response of both filters, measured from the impulse response of
//! GUI-side copies of the same filter classes. Drag a handle: horizontal = cutoff,
//! vertical = resonance.
class FilterResponseDisplay : public QWidget
{
	Q_OBJECT
public:
	FilterResponseDisplay(DualFilterControls* controls, QWidget* parent);
	QSize sizeHint() const override { return QSize(380, 130); }

protected:
	void paintEvent(QPaintEvent*) override;
	void mousePressEvent(QMouseEvent*) override;
	void mouseMoveEvent(QMouseEvent*) override;
	void mouseReleaseEvent(QMouseEvent*) override;

private:
	QPointF handlePosition(int filter) const;
	double freqToX(double freq) const;
	double xToFreq(double x) const;

	DualFilterControls* m_controls;
	int m_dragFilter = -1;
};


} // namespace gui

} // namespace lmms

#endif
