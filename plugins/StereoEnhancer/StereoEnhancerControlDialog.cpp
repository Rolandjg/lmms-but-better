/*
 * StereoEnhancerControlDialog.cpp - control-dialog for StereoEnhancer effect
 *
 * Copyright (c) 2006-2007 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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




#include "StereoEnhancerControlDialog.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <cmath>

#include "FontHelper.h"
#include "Knob.h"
#include "ModernWidgets.h"
#include "StereoEnhancer.h"
#include "StereoEnhancerControls.h"

namespace lmms::gui
{

namespace
{
const QColor BandColors[3] = {modern::secondaryColor(), modern::accentColor(), modern::warningColor()};
constexpr float MinFreq = 20.f;
constexpr float MaxFreq = 20000.f;
}


StereoEnhancerControlDialog::StereoEnhancerControlDialog(
	StereoEnhancerControls * _controls ) :
	EffectControlDialog( _controls )
{
	modern::applyWindowStyle(this);

	auto grid = new QGridLayout(this);
	grid->setContentsMargins(8, 8, 8, 8);
	grid->setSpacing(6);
	grid->setSizeConstraint(QLayout::SetFixedSize);

	grid->addWidget(new ImagerBandDisplay(_controls, this), 0, 0, 1, 2);
	grid->addWidget(new ModernStereoScope(this, &_controls->m_effect->m_scope), 0, 2);
	grid->addWidget(new ModernMeter(this, &_controls->m_outPeakL, &_controls->m_outPeakR), 0, 3);

	auto bands = new ModernSection(tr("Band width"), this);
	bands->grid()->addWidget(modern::makeKnob(this, &_controls->m_lowWidthModel, tr("LOW"),
		tr("Low band width:"), "%"), 0, 0);
	bands->grid()->addWidget(modern::makeKnob(this, &_controls->m_lowCrossoverModel, tr("X-LOW"),
		tr("Low/mid crossover:"), " Hz"), 0, 1);
	bands->grid()->addWidget(modern::makeKnob(this, &_controls->m_midWidthModel, tr("MID"),
		tr("Mid band width:"), "%"), 0, 2);
	bands->grid()->addWidget(modern::makeKnob(this, &_controls->m_highCrossoverModel, tr("X-HIGH"),
		tr("Mid/high crossover:"), " Hz"), 0, 3);
	bands->grid()->addWidget(modern::makeKnob(this, &_controls->m_highWidthModel, tr("HIGH"),
		tr("High band width:"), "%"), 0, 4);
	grid->addWidget(bands, 1, 0, 1, 2);

	auto haas = new ModernSection(tr("Haas"), this);
	haas->grid()->addWidget(modern::makeKnob(this, &_controls->m_widthModel, tr("DELAY"),
		tr("Haas delay:"), " samples"), 0, 0);
	grid->addWidget(haas, 1, 2, 1, 2);
}




ImagerBandDisplay::ImagerBandDisplay(StereoEnhancerControls* controls, QWidget* parent) :
	QWidget(parent),
	m_controls(controls)
{
	setMinimumSize(sizeHint());
	setMouseTracking(true);
	for (Model* m : std::initializer_list<Model*>{&controls->m_lowWidthModel, &controls->m_midWidthModel,
		&controls->m_highWidthModel, &controls->m_lowCrossoverModel, &controls->m_highCrossoverModel})
	{
		connect(m, &Model::dataChanged, this, qOverload<>(&QWidget::update));
	}
}

float ImagerBandDisplay::xToFreq(double x) const
{
	const double norm = std::clamp((x - 6.0) / (width() - 12.0), 0.0, 1.0);
	return static_cast<float>(MinFreq * std::pow(MaxFreq / MinFreq, norm));
}

double ImagerBandDisplay::freqToX(float freq) const
{
	return 6.0 + (width() - 12.0) * std::log(freq / MinFreq) / std::log(MaxFreq / MinFreq);
}

FloatModel* ImagerBandDisplay::bandAt(double x)
{
	if (x < freqToX(m_controls->m_lowCrossoverModel.value())) { return &m_controls->m_lowWidthModel; }
	if (x < freqToX(m_controls->m_highCrossoverModel.value())) { return &m_controls->m_midWidthModel; }
	return &m_controls->m_highWidthModel;
}

void ImagerBandDisplay::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	const QRectF area = QRectF(rect());
	modern::paintDisplay(p, area, 0, 2);
	p.setRenderHint(QPainter::Antialiasing);
	p.setFont(adjustedToPixelSize(font(), SMALL_FONT_SIZE));

	// Decade grid lines
	p.setPen(QPen(modern::gridColor(), 1));
	for (float f : {100.f, 1000.f, 10000.f})
	{
		p.drawLine(QPointF(freqToX(f), area.top() + 3), QPointF(freqToX(f), area.bottom() - 3));
	}

	const double top = area.top() + 16;
	const double bottom = area.bottom() - 16;
	const double unity = (top + bottom) / 2;

	const float x1 = freqToX(m_controls->m_lowCrossoverModel.value());
	const float x2 = freqToX(m_controls->m_highCrossoverModel.value());
	const double edges[4] = {6.0, x1, x2, width() - 6.0};
	FloatModel* models[3] = {&m_controls->m_lowWidthModel, &m_controls->m_midWidthModel, &m_controls->m_highWidthModel};
	const QString names[3] = {tr("LOW"), tr("MID"), tr("HIGH")};

	for (int b = 0; b < 3; ++b)
	{
		const float widthValue = models[b]->value();
		const double y = bottom - (bottom - top) * widthValue / 200.0;
		QColor fill = BandColors[b];
		fill.setAlpha(70);
		const QRectF bar(edges[b] + 2, y, edges[b + 1] - edges[b] - 4, bottom - y);
		p.setPen(Qt::NoPen);
		p.setBrush(fill);
		p.drawRoundedRect(bar, 3, 3);
		p.setPen(QPen(BandColors[b], 2));
		p.drawLine(QPointF(bar.left(), y), QPointF(bar.right(), y));

		p.setPen(modern::textColor());
		p.drawText(QRectF(edges[b], area.top() + 2, edges[b + 1] - edges[b], 14), Qt::AlignCenter,
			QString("%1 %2%").arg(names[b]).arg(widthValue, 0, 'f', 0));
	}

	// 100 % (unchanged) reference
	p.setPen(QPen(QColor(255, 255, 255, 60), 1, Qt::DashLine));
	p.drawLine(QPointF(area.left() + 4, unity), QPointF(area.right() - 4, unity));

	// Crossovers
	p.setPen(QPen(QColor(255, 255, 255, 170), 1.5));
	for (double x : {static_cast<double>(x1), static_cast<double>(x2)})
	{
		p.drawLine(QPointF(x, top - 2), QPointF(x, bottom + 2));
	}

	p.setPen(modern::dimTextColor());
	auto freqLabel = [](float f) { return f >= 1000.f ? QString("%1k").arg(f / 1000.f, 0, 'f', 1) : QString::number(qRound(f)); };
	p.drawText(QRectF(x1 - 40, bottom + 1, 80, 14), Qt::AlignCenter, freqLabel(m_controls->m_lowCrossoverModel.value()));
	p.drawText(QRectF(x2 - 40, bottom + 1, 80, 14), Qt::AlignCenter, freqLabel(m_controls->m_highCrossoverModel.value()));
}

void ImagerBandDisplay::mousePressEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton) { return; }
	const double x = event->pos().x();
	const double x1 = freqToX(m_controls->m_lowCrossoverModel.value());
	const double x2 = freqToX(m_controls->m_highCrossoverModel.value());
	m_dragIsCrossover = true;
	if (std::abs(x - x1) < 5) { m_dragModel = &m_controls->m_lowCrossoverModel; }
	else if (std::abs(x - x2) < 5) { m_dragModel = &m_controls->m_highCrossoverModel; }
	else
	{
		m_dragIsCrossover = false;
		m_dragModel = bandAt(x);
	}
	m_dragModel->addJournalCheckPoint();
	m_lastY = event->pos().y();
}

void ImagerBandDisplay::mouseMoveEvent(QMouseEvent* event)
{
	if (!m_dragModel)
	{
		const double x = event->pos().x();
		const bool nearCrossover = std::abs(x - freqToX(m_controls->m_lowCrossoverModel.value())) < 5
			|| std::abs(x - freqToX(m_controls->m_highCrossoverModel.value())) < 5;
		setCursor(nearCrossover ? Qt::SizeHorCursor : Qt::SizeVerCursor);
		return;
	}
	if (m_dragIsCrossover)
	{
		m_dragModel->setValue(xToFreq(event->pos().x()));
	}
	else
	{
		const double span = height() - 32.0;
		m_dragModel->setValue(m_dragModel->value() - (event->pos().y() - m_lastY) * 200.0 / span);
		m_lastY = event->pos().y();
	}
}

void ImagerBandDisplay::mouseReleaseEvent(QMouseEvent*)
{
	m_dragModel = nullptr;
}

void ImagerBandDisplay::mouseDoubleClickEvent(QMouseEvent* event)
{
	bandAt(event->pos().x())->reset();
}


} // namespace lmms::gui
