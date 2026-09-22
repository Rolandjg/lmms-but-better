/*
 * StereoEnhancerControlDialog.h - control-dialog for stereo-enhancer effect
 *
 * Copyright (c) 2006 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef _STEREOENHANCER_CONTROL_DIALOG_H
#define _STEREOENHANCER_CONTROL_DIALOG_H

#include "EffectControlDialog.h"

namespace lmms
{

class FloatModel;

class StereoEnhancerControls;


namespace gui
{


class StereoEnhancerControlDialog : public EffectControlDialog
{
	Q_OBJECT
public:
	StereoEnhancerControlDialog( StereoEnhancerControls * _controls );
	~StereoEnhancerControlDialog() override = default;

};


//! Three stereo bands on a log frequency axis. Drag a band up/down to change its width,
//! drag a crossover line sideways to move it, double-click a band to reset it.
class ImagerBandDisplay : public QWidget
{
	Q_OBJECT
public:
	ImagerBandDisplay(StereoEnhancerControls* controls, QWidget* parent);
	QSize sizeHint() const override { return QSize(300, 128); }

protected:
	void paintEvent(QPaintEvent*) override;
	void mousePressEvent(QMouseEvent*) override;
	void mouseMoveEvent(QMouseEvent*) override;
	void mouseReleaseEvent(QMouseEvent*) override;
	void mouseDoubleClickEvent(QMouseEvent*) override;

private:
	float xToFreq(double x) const;
	double freqToX(float freq) const;
	FloatModel* bandAt(double x);

	StereoEnhancerControls* m_controls;
	FloatModel* m_dragModel = nullptr;
	bool m_dragIsCrossover = false;
	int m_lastY = 0;
};

} // namespace gui

} // namespace lmms

#endif
