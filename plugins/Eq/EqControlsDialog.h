/*
 * EqControlsDialog.h - definition of EqControlsDialog class.
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

#ifndef EQCONTROLSDIALOG_H
#define EQCONTROLSDIALOG_H

#include <array>

#include <QWidget>

#include "AutomatableModel.h"
#include "EffectControlDialog.h"
#include "EqDisplay.h"

class QLabel;

namespace lmms
{

class EqControls;

namespace gui
{

class Knob;
class ModernHeader;
class ModernSegmented;
class ModernToggle;


//! Numbered chip for one band: tells at a glance which bands are in use, and selects one
class EqBandButton : public QWidget
{
	Q_OBJECT
public:
	EqBandButton(EqDisplay* display, int index, QWidget* parent);

	void setSelected(bool selected);
	QSize sizeHint() const override { return QSize(21, 18); }

signals:
	void selectRequested(int band);

protected:
	void paintEvent(QPaintEvent*) override;
	void mousePressEvent(QMouseEvent*) override;
	void mouseDoubleClickEvent(QMouseEvent*) override;

private:
	EqDisplay* m_display;
	int m_index;
	bool m_selected = false;
};


//! The equalizer window: the curve, and controls mirroring whichever band is selected in it
class EqControlsDialog : public EffectControlDialog
{
	Q_OBJECT
public:
	explicit EqControlsDialog(EqControls* controls);

private slots:
	//! Points the controls below the curve at another band
	void showBand(int band);

private:
	QWidget* createBandSection();
	QWidget* createAnalyserSection();
	QWidget* createLevelSection();

	EqControls* m_controls;
	EqDisplay* m_display;
	QLabel* m_bandName = nullptr;
	ModernToggle* m_bandActive = nullptr;
	Knob* m_freqKnob = nullptr;
	Knob* m_gainKnob = nullptr;
	Knob* m_qKnob = nullptr;
	ModernSegmented* m_slope = nullptr;
	ModernHeader* m_slopeTitle = nullptr;
	std::array<EqBandButton*, EqDisplay::BandCount> m_bandButtons{};

	//! Stand-ins for the bands without a gain or a slope, so the controls can keep their place
	FloatModel m_noGain;
	IntModel m_noSlope;
};


} // namespace gui

} // namespace lmms

#endif // EQCONTROLSDIALOG_H
