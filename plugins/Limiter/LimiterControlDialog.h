/*
 * LimiterControlDialog.h - editor for the lookahead brickwall limiter
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

#ifndef LMMS_GUI_LIMITER_CONTROL_DIALOG_H
#define LMMS_GUI_LIMITER_CONTROL_DIALOG_H

#include "EffectControlDialog.h"

namespace lmms
{

class LimiterControls;

namespace gui
{

class LimiterControlDialog : public EffectControlDialog
{
	Q_OBJECT
public:
	LimiterControlDialog(LimiterControls* controls);
	~LimiterControlDialog() override = default;
};


//! Scrolling picture of the last few seconds: input level, output level and the gain reduction hanging from
//! the top. Drag the ceiling line to move it, drag anywhere else to push the input gain.
class LimiterDisplay : public QWidget
{
	Q_OBJECT
public:
	LimiterDisplay(LimiterControls* controls, QWidget* parent);
	QSize sizeHint() const override { return QSize(400, 180); }

protected:
	void paintEvent(QPaintEvent*) override;
	void mousePressEvent(QMouseEvent*) override;
	void mouseMoveEvent(QMouseEvent*) override;
	void mouseReleaseEvent(QMouseEvent*) override;

private:
	QRectF plotRect() const;
	float dbToY(float db, const QRectF& plot) const;
	bool nearCeiling(const QPointF& pos) const;

	LimiterControls* m_controls;
	enum class Drag { None, Ceiling, Gain } m_drag = Drag::None;
	float m_dragStartY = 0.f;
	float m_valueAtPress = 0.f;
	float m_displayGainDb = 0.f;
};


//! Vertical gain reduction meter with a peak hold line
class LimiterReductionMeter : public QWidget
{
	Q_OBJECT
public:
	LimiterReductionMeter(LimiterControls* controls, QWidget* parent);
	QSize sizeHint() const override { return QSize(18, 180); }

protected:
	void paintEvent(QPaintEvent*) override;

private:
	LimiterControls* m_controls;
	float m_display = 0.f;
	float m_hold = 0.f;
	int m_holdCounter = 0;
};

} // namespace gui

} // namespace lmms

#endif // LMMS_GUI_LIMITER_CONTROL_DIALOG_H
