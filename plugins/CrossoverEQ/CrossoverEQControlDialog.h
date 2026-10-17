/*
 * CrossoverEQControlDialog.h - A native 4-band Crossover Equalizer 
 * good for simulating tonestacks or simple peakless (flat-band) equalization
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

#ifndef CROSSOVEREQ_CONTROL_DIALOG_H
#define CROSSOVEREQ_CONTROL_DIALOG_H

#include "EffectControlDialog.h"

#include <QWidget>

namespace lmms
{


class CrossoverEQControls;
class FloatModel;


namespace gui
{

class CrossoverEQControlDialog : public EffectControlDialog
{
	Q_OBJECT
public:
	CrossoverEQControlDialog( CrossoverEQControls * controls );
	~CrossoverEQControlDialog() override = default;
};


//! The four bands on a frequency axis with the combined response of the Linkwitz-Riley split.
//! Crossover lines drag sideways, bands drag up and down for gain (double-click resets).
class CrossoverEQDisplay : public QWidget
{
	Q_OBJECT
public:
	CrossoverEQDisplay(CrossoverEQControls* controls, QWidget* parent);
	QSize sizeHint() const override { return QSize(400, 150); }

	static QColor bandColor(int band);

protected:
	void paintEvent(QPaintEvent*) override;
	void mousePressEvent(QMouseEvent*) override;
	void mouseMoveEvent(QMouseEvent*) override;
	void mouseReleaseEvent(QMouseEvent*) override;
	void mouseDoubleClickEvent(QMouseEvent*) override;

private:
	QRectF plotRect() const;
	double toX(double freq) const;
	double toFreq(double x) const;
	double toY(double db) const;
	double toDb(double y) const;
	FloatModel* crossover(int index) const;
	FloatModel* gain(int band) const;
	bool plays(int band) const;
	int crossoverAt(const QPointF& pos) const;
	int bandAt(const QPointF& pos) const;

	CrossoverEQControls* m_controls;
	int m_dragCrossover = -1;
	int m_dragBand = -1;
	int m_hoverCrossover = -1;
	double m_dragStartY = 0;
	float m_dragStartGain = 0;
};


} // namespace gui

} // namespace lmms

#endif
