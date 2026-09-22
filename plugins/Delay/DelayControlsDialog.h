/*
 * DelayControlsDialog.h - declaration of DelayControlsDialog class.
 *
 * Copyright (c) 2014 David French <dave/dot/french3/at/googlemail/dot/com>
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

#ifndef DELAYCONTROLSDIALOG_H
#define DELAYCONTROLSDIALOG_H

#include "EffectControlDialog.h"

namespace lmms
{


class DelayControls;

namespace gui
{

class DelayControlsDialog : public EffectControlDialog
{
	Q_OBJECT
public:
	DelayControlsDialog( DelayControls* controls );
	~DelayControlsDialog() override = default;
};


//! Shows the echo pattern that the current settings produce.
//! Drag horizontally to change the delay time and vertically to change the feedback.
class DelayTapDisplay : public QWidget
{
	Q_OBJECT
public:
	DelayTapDisplay(DelayControls* controls, QWidget* parent);

	QSize sizeHint() const override { return QSize(320, 104); }

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;

private:
	DelayControls* m_controls;
	QPoint m_lastPos;
	bool m_dragging = false;
};


} // namespace gui

} // namespace lmms

#endif // DELAYCONTROLSDIALOG_H
