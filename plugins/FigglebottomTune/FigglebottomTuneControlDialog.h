/*
 * FigglebottomTuneControlDialog.h - editor for the FigglebottomTune pitch corrector
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

#ifndef LMMS_GUI_FIGGLEBOTTOM_TUNE_CONTROL_DIALOG_H
#define LMMS_GUI_FIGGLEBOTTOM_TUNE_CONTROL_DIALOG_H

#include <array>

#include <QElapsedTimer>

#include "EffectControlDialog.h"
#include "WheelAccumulator.h"

namespace lmms
{

class FigglebottomTuneControls;

namespace gui
{

class FigglebottomTuneControlDialog : public EffectControlDialog
{
	Q_OBJECT
public:
	FigglebottomTuneControlDialog(FigglebottomTuneControls* controls);
	~FigglebottomTuneControlDialog() override = default;
};


//! Scrolling piano roll of the pitch track: the sung pitch as a thin line, the corrected pitch
//! as a thick one in the colour of the note it snaps to, and the correction shaded between
//! them. Click (or drag over) the keys to switch scale notes, scroll to move up and down,
//! Ctrl+scroll to zoom, double-click to follow the voice again.
class PitchRoll : public QWidget
{
	Q_OBJECT
public:
	PitchRoll(FigglebottomTuneControls* controls, QWidget* parent);

	QSize sizeHint() const override { return QSize(600, 250); }

	//! Each pitch class gets its own hue around the colour wheel, C at red
	static QColor noteColor(int pitchClass, float saturation = 0.5f, float value = 1.f);

protected:
	void paintEvent(QPaintEvent*) override;
	void mousePressEvent(QMouseEvent*) override;
	void mouseMoveEvent(QMouseEvent*) override;
	void mouseReleaseEvent(QMouseEvent*) override;
	void mouseDoubleClickEvent(QMouseEvent*) override;
	void wheelEvent(QWheelEvent*) override;

private:
	void tick();
	QRectF keysRect() const;
	QRectF plotRect() const;
	float yForNote(float note) const;
	float noteAt(float y) const;
	void paintKeys(QPainter& p, int activeTarget);

	FigglebottomTuneControls* m_controls;

	float m_bottomNote = 48.f; //!< lowest visible pitch (fractional while scrolling)
	float m_visibleNotes = 24.f;
	bool m_follow = true;
	float m_followTarget = 48.f; //!< bottom note the view is easing towards while following
	bool m_dragging = false;
	float m_dragStartY = 0.f;
	float m_dragStartBottom = 0.f;

	std::array<float, 12> m_glow{}; //!< per pitch class, lights up while snapped to and fades
	std::uint64_t m_lastCount = 0;
	QElapsedTimer m_sinceNewData;

	int m_paintingKeys = -1; //!< while dragging over the keys: 1 switches notes on, 0 off
	int m_lastPaintedNote = -1000;

private:
	WheelAccumulator m_wheelSteps;
};

} // namespace gui

} // namespace lmms

#endif // LMMS_GUI_FIGGLEBOTTOM_TUNE_CONTROL_DIALOG_H
