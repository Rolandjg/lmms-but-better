/*
 * EqControlsDialog.cpp - implementation of EqControlsDialog class.
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

#include "EqControlsDialog.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>

#include "EqControls.h"
#include "FontHelper.h"
#include "GuiApplication.h"
#include "Knob.h"
#include "MainWindow.h"
#include "ModernWidgets.h"

namespace lmms::gui
{


EqBandButton::EqBandButton(EqDisplay* display, int index, QWidget* parent) :
	QWidget(parent),
	m_display(display),
	m_index(index)
{
	setFixedSize(sizeHint());
	setCursor(Qt::PointingHandCursor);
	const EqBand& band = display->band(index);
	setToolTip(tr("%1: click to edit it, double-click to switch it on or off").arg(band.name));
	connect(band.active, &Model::dataChanged, this, [this] { update(); });
}




void EqBandButton::setSelected(bool selected)
{
	if (selected == m_selected) { return; }
	m_selected = selected;
	update();
}




void EqBandButton::paintEvent(QPaintEvent*)
{
	const EqBand& band = m_display->band(m_index);
	const bool active = band.active->value();

	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);

	QColor fill = active ? band.color : modern::buttonColor();
	if (active) { fill.setAlpha(60); }
	p.setPen(QPen(m_selected ? modern::textColor() : active ? band.color : modern::sectionBorder(),
		m_selected ? 1.5 : 1));
	p.setBrush(fill);
	p.drawRoundedRect(QRectF(rect()).adjusted(0.75, 0.75, -0.75, -0.75), 2, 2);

	p.setFont(adjustedToPixelSize(font(), SMALL_FONT_SIZE));
	p.setPen(active ? band.color : modern::dimTextColor());
	p.drawText(rect(), Qt::AlignCenter, QString::number(m_index + 1));
}




void EqBandButton::mousePressEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton) { emit selectRequested(m_index); }
	else { QWidget::mousePressEvent(event); }
}




void EqBandButton::mouseDoubleClickEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton)
	{
		QWidget::mouseDoubleClickEvent(event);
		return;
	}
	BoolModel* active = m_display->band(m_index).active;
	active->setValue(!active->value());
	emit selectRequested(m_index);
}




EqControlsDialog::EqControlsDialog(EqControls* controls) :
	EffectControlDialog(controls),
	m_controls(controls),
	m_display(new EqDisplay(controls, this)),
	m_noGain(0.f, -18.f, 18.f, 0.01f, nullptr, tr("Gain")),
	m_noSlope(0, 0, 2, nullptr, tr("Slope"))
{
	modern::applyWindowStyle(this);

	auto root = new QVBoxLayout(this);
	root->setContentsMargins(8, 8, 8, 8);
	root->setSpacing(6);
	root->addWidget(m_display, 1);

	auto bottom = new QHBoxLayout();
	bottom->setSpacing(6);
	bottom->addWidget(createBandSection(), 1);
	bottom->addWidget(createAnalyserSection());
	bottom->addWidget(createLevelSection());
	root->addLayout(bottom);

	connect(m_display, &EqDisplay::bandSelected, this, &EqControlsDialog::showBand);
	showBand(m_display->selectedBand());

	// The audio thread only ever raises these, so the meters need them brought back down
	connect(getGUI()->mainWindow(), &MainWindow::periodicUpdate, this, [controls]
	{
		for (float* peak : {&controls->m_inPeakL, &controls->m_inPeakR, &controls->m_outPeakL,
			&controls->m_outPeakR})
		{
			*peak *= 0.75f;
		}
	});

	setMinimumSize(560, 340);
	resize(780, 470);
}




QWidget* EqControlsDialog::createBandSection()
{
	auto section = new ModernSection(tr("Band"), this);
	auto grid = section->grid();

	auto strip = new QHBoxLayout();
	strip->setSpacing(3);
	for (int i = 0; i < EqDisplay::BandCount; ++i)
	{
		auto button = new EqBandButton(m_display, i, section);
		connect(button, &EqBandButton::selectRequested, m_display, &EqDisplay::selectBand);
		m_bandButtons[i] = button;
		strip->addWidget(button);
	}
	strip->addSpacing(8);

	m_bandName = new QLabel(section);
	QFont nameFont = adjustedToPixelSize(font(), SMALL_FONT_SIZE);
	nameFont.setBold(true);
	m_bandName->setFont(nameFont);
	strip->addWidget(m_bandName);
	strip->addSpacing(4);

	m_bandActive = new ModernToggle(tr("ON"), section);
	m_bandActive->setToolTip(tr("Switch the selected band on or off"));
	strip->addWidget(m_bandActive);
	strip->addStretch(1);
	grid->addLayout(strip, 0, 0, 1, 4);

	m_freqKnob = modern::makeKnob(section, m_display->band(0).freq, tr("FREQ"), tr("Frequency:"), " Hz");
	m_gainKnob = modern::makeKnob(section, &m_noGain, tr("GAIN"), tr("Gain:"), " dB");
	m_qKnob = modern::makeKnob(section, m_display->band(0).q, tr("Q"), tr("Resonance:"), "");
	grid->addWidget(m_freqKnob, 1, 0, Qt::AlignBottom);
	grid->addWidget(m_gainKnob, 1, 1, Qt::AlignBottom);
	grid->addWidget(m_qKnob, 1, 2, Qt::AlignBottom);

	auto slopeColumn = new QVBoxLayout();
	slopeColumn->setSpacing(2);
	m_slopeTitle = new ModernHeader(tr("Slope"), section);
	slopeColumn->addWidget(m_slopeTitle);
	m_slope = new ModernSegmented(section);
	m_slope->setLabels({tr("12"), tr("24"), tr("48")});
	m_slope->setToolTips({tr("12 dB per octave"), tr("24 dB per octave"), tr("48 dB per octave")});
	m_slope->setModel(&m_noSlope);
	slopeColumn->addWidget(m_slope);
	grid->addLayout(slopeColumn, 1, 3, Qt::AlignBottom);
	grid->setColumnStretch(4, 1);

	return section;
}




QWidget* EqControlsDialog::createAnalyserSection()
{
	auto section = new ModernSection(tr("Analyser"), this);
	auto grid = section->grid();

	auto in = new ModernToggle(tr("IN"), section);
	in->setModel(&m_controls->m_analyseInModel);
	in->setToolTip(tr("Show the spectrum going in"));
	auto out = new ModernToggle(tr("OUT"), section);
	out->setModel(&m_controls->m_analyseOutModel);
	out->setToolTip(tr("Show the spectrum coming out"));
	grid->addWidget(in, 0, 0);
	grid->addWidget(out, 0, 1);

	auto channelMode = new ModernSegmented(section);
	channelMode->setModel(&m_controls->m_channelModeModel);
	channelMode->setToolTips({tr("Bands process left and right"), tr("Bands process only the mid (mono) part"),
		tr("Bands process only the side (stereo) part")});
	grid->addWidget(channelMode, 1, 0, 1, 2);

	return section;
}




QWidget* EqControlsDialog::createLevelSection()
{
	auto section = new ModernSection(tr("Level"), this);
	auto grid = section->grid();

	const auto addMeter = [&](int column, FloatModel* model, const QString& label, const float* peakL,
		const float* peakR)
	{
		auto knob = modern::makeKnob(section, model, label, tr("Gain:"), " dB");
		grid->addWidget(knob, 0, column, Qt::AlignBottom);
		auto meter = new ModernMeter(section, peakL, peakR);
		meter->setFixedSize(10, 44);
		grid->addWidget(meter, 0, column + 1, Qt::AlignBottom);
	};

	addMeter(0, &m_controls->m_inGainModel, tr("IN"), &m_controls->m_inPeakL, &m_controls->m_inPeakR);
	addMeter(2, &m_controls->m_outGainModel, tr("OUT"), &m_controls->m_outPeakL, &m_controls->m_outPeakR);

	return section;
}




void EqControlsDialog::showBand(int band)
{
	const EqBand& selected = m_display->band(band);
	m_bandName->setText(selected.name);
	m_bandName->setStyleSheet(QString("color: %1;").arg(selected.color.name()));

	m_bandActive->setModel(selected.active);
	m_freqKnob->setModel(selected.freq);
	m_gainKnob->setModel(selected.gain ? selected.gain : &m_noGain);
	m_gainKnob->setEnabled(selected.hasGain());
	m_qKnob->setModel(selected.q);

	// the peaks are set by bandwidth in octaves, the shelves and pass filters by resonance
	const bool peak = selected.type == EqBandType::Peak;
	m_qKnob->setLabel(peak ? tr("BW") : tr("Q"));
	m_qKnob->setHintText(peak ? tr("Bandwidth:") : tr("Resonance:"), peak ? tr(" octaves") : "");

	// only the pass filters have a slope, and an empty cell says so more clearly than a dead control
	m_slope->setModel(selected.slope ? selected.slope : &m_noSlope);
	m_slope->setVisible(selected.slope != nullptr);
	m_slopeTitle->setVisible(selected.slope != nullptr);

	for (int i = 0; i < EqDisplay::BandCount; ++i) { m_bandButtons[i]->setSelected(i == band); }
}


} // namespace lmms::gui
