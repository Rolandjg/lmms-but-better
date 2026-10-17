/*
 * PumperControlDialog.h - editor for the tempo-synced volume shaper
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

#ifndef LMMS_GUI_PUMPER_CONTROL_DIALOG_H
#define LMMS_GUI_PUMPER_CONTROL_DIALOG_H

#include <QPointF>

#include "EffectControlDialog.h"

namespace lmms
{

class PumperControls;

namespace gui
{

class PumperControlDialog : public EffectControlDialog
{
	Q_OBJECT
public:
	PumperControlDialog(PumperControls* controls);
	~PumperControlDialog() override = default;
};


//! Shows one cycle of the volume shape with a live playhead. Drag up/down for depth and
//! left/right for release.
class PumperShapeDisplay : public QWidget
{
	Q_OBJECT
public:
	PumperShapeDisplay(PumperControls* controls, QWidget* parent);
	QSize sizeHint() const override { return QSize(320, 130); }

protected:
	void paintEvent(QPaintEvent*) override;
	void mousePressEvent(QMouseEvent*) override;
	void mouseMoveEvent(QMouseEvent*) override;

private:
	PumperControls* m_controls;
	QPointF m_dragStart;
	float m_depthAtPress = 0.f;
	float m_releaseAtPress = 0.f;
};

} // namespace gui

} // namespace lmms

#endif // LMMS_GUI_PUMPER_CONTROL_DIALOG_H
