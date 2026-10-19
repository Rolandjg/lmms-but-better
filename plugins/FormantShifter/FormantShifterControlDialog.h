/*
 * FormantShifterControlDialog.h - editor for the formant shifter / vowel filter
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

#ifndef LMMS_GUI_FORMANT_SHIFTER_CONTROL_DIALOG_H
#define LMMS_GUI_FORMANT_SHIFTER_CONTROL_DIALOG_H

#include <vector>

#include <QPointF>

#include "AutomatableModelView.h"
#include "EffectControlDialog.h"
#include "WheelAccumulator.h"

namespace lmms
{

class FormantShifterControls;

namespace gui
{

class FormantShifterControlDialog : public EffectControlDialog
{
	Q_OBJECT
public:
	FormantShifterControlDialog(FormantShifterControls* controls);
	~FormantShifterControlDialog() override = default;
};


//! Input and output spectrum with the formant envelopes (spectral mode) or the vowel filter's
//! response (resonant mode). Drag sideways to shift the formants, scroll to morph the vowel,
//! double-click to reset the shift.
class FormantDisplay : public QWidget
{
	Q_OBJECT
public:
	FormantDisplay(FormantShifterControls* controls, QWidget* parent);
	~FormantDisplay() override;

	QSize sizeHint() const override { return QSize(520, 180); }

protected:
	void paintEvent(QPaintEvent*) override;
	void mousePressEvent(QMouseEvent*) override;
	void mouseMoveEvent(QMouseEvent*) override;
	void mouseDoubleClickEvent(QMouseEvent*) override;
	void wheelEvent(QWheelEvent*) override;

private:
	void analyse();
	QRectF plotRect() const;
	float xForFrequency(const QRectF& plot, float freq) const;
	float yForDb(const QRectF& plot, float db) const;

	FormantShifterControls* m_controls;
	std::vector<float> m_inputSpectrum;  //!< smoothed dBFS per display point
	std::vector<float> m_outputSpectrum;
	float* m_fftIn = nullptr;
	void* m_fftOut = nullptr;
	void* m_plan = nullptr;
	QPointF m_dragStart;
	float m_formantAtPress = 0.f;

private:
	WheelAccumulator m_wheelSteps;
};


//! The vowel morph as a strip of letters: click or drag anywhere to set the position, double-click
//! a letter to snap to it. A ghost marker shows where the LFO and envelope currently take it.
class VowelStrip : public QWidget, public FloatModelView
{
	Q_OBJECT
public:
	VowelStrip(FormantShifterControls* controls, QWidget* parent);

	QSize sizeHint() const override { return QSize(260, 24); }
	void modelChanged() override { update(); }

protected:
	void paintEvent(QPaintEvent*) override;
	void mousePressEvent(QMouseEvent*) override;
	void mouseMoveEvent(QMouseEvent*) override;
	void mouseDoubleClickEvent(QMouseEvent*) override;
	void wheelEvent(QWheelEvent*) override;
	void contextMenuEvent(QContextMenuEvent*) override;

private:
	float valueAt(float x) const;
	float xFor(float value) const;

	FormantShifterControls* m_controls;

private:
	WheelAccumulator m_wheelSteps;
};

} // namespace gui

} // namespace lmms

#endif // LMMS_GUI_FORMANT_SHIFTER_CONTROL_DIALOG_H
