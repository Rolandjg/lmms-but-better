/*
 * BassBoosterControlDialog.h - control dialog for bassbooster effect
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

#ifndef _BASSBOOSTER_CONTROL_DIALOG_H
#define _BASSBOOSTER_CONTROL_DIALOG_H

#include "EffectControlDialog.h"

#include <QWidget>

namespace lmms
{

class BassBoosterControls;

namespace gui
{

class BassBoosterControlDialog : public EffectControlDialog
{
	Q_OBJECT
public:
	BassBoosterControlDialog( BassBoosterControls* controls );
	~BassBoosterControlDialog() override = default;

} ;


//! Magnitude response of the boost filter. The handle sits on the corner frequency at the
//! low-end boost: drag sideways for frequency, up and down for the boost amount.
class BassBoosterDisplay : public QWidget
{
	Q_OBJECT
public:
	BassBoosterDisplay(BassBoosterControls* controls, QWidget* parent);
	QSize sizeHint() const override { return QSize(300, 132); }

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
	//! Response in dB at @p freq, including the output gain
	double responseDb(double freq) const;
	QPointF handlePos() const;

	BassBoosterControls* m_controls;
	bool m_dragging = false;
	bool m_hover = false;
};


} // namespace gui

} // namespace lmms

#endif
