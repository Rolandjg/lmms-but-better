/*
 * CrossoverEQControlDialog.cpp - A native 4-band Crossover Equalizer 
 * good for simulating tonestacks or simple peakless (flat-band) equalization
 *
 * Copyright (c) 2014 Vesa Kivimäki <contact/dot/diizy/at/nbl/dot/fi>
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


#include "CrossoverEQControlDialog.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>
#include <complex>

#include "AutomatableButton.h"
#include "CrossoverEQControls.h"
#include "DeprecationHelper.h"
#include "Fader.h"
#include "FontHelper.h"
#include "Knob.h"
#include "ModernWidgets.h"


namespace lmms::gui
{

namespace
{
constexpr double MinFreq = 20.0;
constexpr double MaxFreq = 20000.0;
constexpr double MinDb = -36.0;
constexpr double MaxDb = 24.0;

QString formatFreq(double hz)
{
	return hz >= 1000.0 ? QString("%1k").arg(hz / 1000.0, 0, 'f', hz >= 10000.0 ? 0 : 1) : QString::number(qRound(hz));
}

//! Analog 4th-order Linkwitz-Riley (two squared Butterworth sections) at frequency f
std::complex<double> lr4(double f, double corner, bool highpass)
{
	const std::complex<double> s(0.0, f / corner);
	const auto butterworth = 1.0 / (1.0 + std::sqrt(2.0) * s + s * s);
	const auto section = highpass ? s * s * butterworth : butterworth;
	return section * section;
}
}


CrossoverEQControlDialog::CrossoverEQControlDialog(CrossoverEQControls *controls) :
	EffectControlDialog(controls)
{
	modern::applyWindowStyle(this);

	auto root = new QVBoxLayout(this);
	root->setContentsMargins(8, 8, 8, 8);
	root->setSpacing(6);
	root->setSizeConstraint(QLayout::SetFixedSize);

	root->addWidget(new CrossoverEQDisplay(controls, this));

	auto row = new QHBoxLayout();
	row->setSpacing(6);
	root->addLayout(row);

	auto crossovers = new ModernSection(tr("Split"), this);
	const std::array<std::pair<FloatModel*, QString>, 3> xovers = {{
		{&controls->m_xover12, "1/2"}, {&controls->m_xover23, "2/3"}, {&controls->m_xover34, "3/4"}}};
	for (int i = 0; i < 3; ++i)
	{
		crossovers->grid()->addWidget(modern::makeKnob(this, xovers[i].first, xovers[i].second,
			tr("Band %1 crossover:").arg(xovers[i].second), " Hz"), i, 0);
	}
	row->addWidget(crossovers);

	const std::array<FloatModel*, 4> gains = {&controls->m_gain1, &controls->m_gain2, &controls->m_gain3, &controls->m_gain4};
	const std::array<BoolModel*, 4> enabled = {&controls->m_mute1, &controls->m_mute2, &controls->m_mute3, &controls->m_mute4};
	const std::array<QString, 4> names = {tr("Low"), tr("Low mid"), tr("High mid"), tr("High")};
	for (int i = 0; i < 4; ++i)
	{
		auto band = new ModernSection(names[i], this);
		auto grid = band->grid();

		auto fader = new Fader(gains[i], tr("Band %1 gain").arg(i + 1), this, false);
		fader->setHintText(tr("Band %1 gain:").arg(i + 1), " dBFS");
		fader->setDisplayConversion(false);
		fader->setRenderUnityLine(false);
		grid->addWidget(fader, 0, 0, 1, 2, Qt::AlignHCenter);
		// The fader's own level bars show what the band puts out
		auto meterTimer = new QTimer(fader);
		connect(meterTimer, &QTimer::timeout, fader, [fader, controls, i] {
			fader->setPeak_L(controls->m_bandPeakL[i]);
			fader->setPeak_R(controls->m_bandPeakR[i]);
		});
		meterTimer->start(33);

		auto buttons = new QHBoxLayout();
		buttons->setSpacing(4);
		auto mute = new AutomatableButton(this, tr("Mute band %1").arg(i + 1));
		mute->setCheckable(true);
		mute->setModel(enabled[i]);
		mute->setToolTip(tr("Mute band %1").arg(i + 1));
		mute->setObjectName("btn-mute-inv");
		buttons->addWidget(mute);
		auto solo = new AutomatableButton(this, tr("Solo band %1").arg(i + 1));
		solo->setCheckable(true);
		solo->setModel(&controls->m_solo[i]);
		solo->setToolTip(tr("Solo band %1").arg(i + 1));
		solo->setObjectName("btn-solo");
		buttons->addWidget(solo);
		grid->addLayout(buttons, 1, 0, 1, 2, Qt::AlignCenter);

		grid->addWidget(modern::makeKnob(this, &controls->m_width[i], tr("WIDTH"),
			tr("Band %1 stereo width:").arg(i + 1), "%"), 2, 0, 1, 2, Qt::AlignCenter);
		row->addWidget(band);
	}
}




CrossoverEQDisplay::CrossoverEQDisplay(CrossoverEQControls* controls, QWidget* parent) :
	QWidget(parent),
	m_controls(controls)
{
	setMinimumSize(sizeHint());
	setMouseTracking(true);
	setToolTip(tr("Drag a crossover line to move it, drag a band up or down to change its gain.\n"
		"Double-click a band to reset its gain."));
	auto timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, qOverload<>(&QWidget::update));
	timer->start(50);
}




QColor CrossoverEQDisplay::bandColor(int band)
{
	static const std::array<QColor, 4> colors = {
		modern::secondaryColor(), modern::accentColor(), modern::warningColor(), QColor(240, 150, 70)};
	return colors[band];
}

QRectF CrossoverEQDisplay::plotRect() const { return QRectF(rect()).adjusted(28, 8, -8, -20); }

double CrossoverEQDisplay::toX(double freq) const
{
	const QRectF plot = plotRect();
	return plot.left() + plot.width() * std::log(freq / MinFreq) / std::log(MaxFreq / MinFreq);
}

double CrossoverEQDisplay::toFreq(double x) const
{
	const QRectF plot = plotRect();
	return MinFreq * std::pow(MaxFreq / MinFreq, (x - plot.left()) / plot.width());
}

double CrossoverEQDisplay::toY(double db) const
{
	const QRectF plot = plotRect();
	return plot.bottom() - plot.height() * (std::clamp(db, MinDb, MaxDb) - MinDb) / (MaxDb - MinDb);
}

double CrossoverEQDisplay::toDb(double y) const
{
	const QRectF plot = plotRect();
	return MinDb + (plot.bottom() - y) / plot.height() * (MaxDb - MinDb);
}

FloatModel* CrossoverEQDisplay::crossover(int index) const
{
	const std::array<FloatModel*, 3> models = {&m_controls->m_xover12, &m_controls->m_xover23, &m_controls->m_xover34};
	return models[index];
}

FloatModel* CrossoverEQDisplay::gain(int band) const
{
	const std::array<FloatModel*, 4> models = {&m_controls->m_gain1, &m_controls->m_gain2, &m_controls->m_gain3, &m_controls->m_gain4};
	return models[band];
}

bool CrossoverEQDisplay::plays(int band) const
{
	bool anySolo = false;
	for (const auto& solo : m_controls->m_solo) { anySolo = anySolo || solo.value(); }
	if (anySolo) { return m_controls->m_solo[band].value(); }
	const std::array<const BoolModel*, 4> enabled = {&m_controls->m_mute1, &m_controls->m_mute2, &m_controls->m_mute3, &m_controls->m_mute4};
	return enabled[band]->value();
}

int CrossoverEQDisplay::crossoverAt(const QPointF& pos) const
{
	if (!plotRect().adjusted(0, -8, 0, 8).contains(pos)) { return -1; }
	int best = -1;
	double bestDistance = 6.0;
	for (int i = 0; i < 3; ++i)
	{
		const double distance = std::abs(pos.x() - toX(crossover(i)->value()));
		if (distance < bestDistance) { best = i; bestDistance = distance; }
	}
	return best;
}

int CrossoverEQDisplay::bandAt(const QPointF& pos) const
{
	if (!plotRect().contains(pos)) { return -1; }
	const double freq = toFreq(pos.x());
	for (int i = 0; i < 3; ++i)
	{
		if (freq < crossover(i)->value()) { return i; }
	}
	return 3;
}




void CrossoverEQDisplay::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	const QRectF area = QRectF(rect());
	const QRectF plot = plotRect();
	modern::paintDisplay(p, area, 0, 0);
	p.setRenderHint(QPainter::Antialiasing);
	p.setFont(adjustedToPixelSize(font(), SMALL_FONT_SIZE));
	const QFontMetricsF fm(p.font());

	const std::array<double, 5> edges = {MinFreq, crossover(0)->value(), crossover(1)->value(), crossover(2)->value(), MaxFreq};

	// Band regions
	for (int i = 0; i < 4; ++i)
	{
		const QRectF region(QPointF(toX(edges[i]), plot.top()), QPointF(toX(edges[i + 1]), plot.bottom()));
		QColor tint = bandColor(i);
		tint.setAlpha(plays(i) ? 22 : 6);
		p.fillRect(region, tint);
		if (!plays(i))
		{
			p.fillRect(region, QBrush(QColor(255, 255, 255, 10), Qt::BDiagPattern));
		}

		// Live band level along the bottom edge
		const float peak = std::max(m_controls->m_bandPeakL[i], m_controls->m_bandPeakR[i]);
		const double level = std::clamp((20.0 * std::log10(std::max(peak, 1e-6f)) + 60.0) / 60.0, 0.0, 1.0);
		QColor meter = bandColor(i);
		meter.setAlpha(150);
		p.fillRect(QRectF(region.left() + 1, plot.bottom() - 3, (region.width() - 2) * level, 3), meter);
	}

	// Grid
	p.setPen(QPen(modern::gridColor(), 1));
	for (double f : {50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0})
	{
		p.drawLine(QPointF(toX(f), plot.top()), QPointF(toX(f), plot.bottom()));
	}
	for (double db : {-24.0, -12.0, 12.0}) { p.drawLine(QPointF(plot.left(), toY(db)), QPointF(plot.right(), toY(db))); }
	p.setPen(QPen(QColor(255, 255, 255, 50), 1, Qt::DashLine));
	p.drawLine(QPointF(plot.left(), toY(0)), QPointF(plot.right(), toY(0)));
	p.setPen(modern::dimTextColor());
	for (double db : {-24.0, -12.0, 0.0, 12.0})
	{
		const QString text = db > 0 ? QString("+%1").arg(db) : QString::number(db);
		p.drawText(QPointF(plot.left() - 5 - fm.horizontalAdvance(text), toY(db) + fm.capHeight() / 2), text);
	}
	for (double f : {100.0, 1000.0, 10000.0})
	{
		const QString text = formatFreq(f);
		p.drawText(QPointF(toX(f) - fm.horizontalAdvance(text) / 2, plot.bottom() + 4 + fm.capHeight()), text);
	}

	// Combined response of the bands that are playing
	std::array<double, 4> gains{};
	for (int i = 0; i < 4; ++i) { gains[i] = plays(i) ? std::pow(10.0, gain(i)->value() / 20.0) : 0.0; }
	QPainterPath curve;
	const int steps = static_cast<int>(plot.width());
	for (int i = 0; i <= steps; ++i)
	{
		const double x = plot.left() + i;
		const double f = toFreq(x);
		const auto low = lr4(f, edges[2], false);
		const auto high = lr4(f, edges[2], true);
		const auto h = gains[0] * low * lr4(f, edges[1], false) + gains[1] * low * lr4(f, edges[1], true)
			+ gains[2] * high * lr4(f, edges[3], false) + gains[3] * high * lr4(f, edges[3], true);
		const QPointF pt(x, toY(20.0 * std::log10(std::max(std::abs(h), 1e-6))));
		if (i == 0) { curve.moveTo(pt); } else { curve.lineTo(pt); }
	}
	p.setPen(QPen(QColor(255, 255, 255, 220), 1.6));
	p.drawPath(curve);

	// Gain handles and band names
	for (int i = 0; i < 4; ++i)
	{
		const double center = toX(std::sqrt(edges[i] * edges[i + 1]));
		const double y = toY(gain(i)->value());
		QColor c = bandColor(i);
		if (!plays(i)) { c.setAlpha(90); }
		p.setPen(QPen(modern::displayColor(), 1.5));
		p.setBrush(c);
		p.drawEllipse(QPointF(center, y), m_dragBand == i ? 6.0 : 4.5, m_dragBand == i ? 6.0 : 4.5);

		const QString value = QString("%1%2 dB").arg(gain(i)->value() > 0 ? "+" : "").arg(gain(i)->value(), 0, 'f', 1);
		const double width = fm.horizontalAdvance(value);
		if (toX(edges[i + 1]) - toX(edges[i]) > width + 6)
		{
			p.setPen(c);
			const double textY = y > plot.top() + 18 ? y - 9 : y + 9 + fm.capHeight();
			p.drawText(QPointF(center - width / 2, textY), value);
		}
	}

	// Crossover lines
	for (int i = 0; i < 3; ++i)
	{
		const double x = toX(edges[i + 1]);
		const bool active = m_dragCrossover == i || m_hoverCrossover == i;
		p.setPen(QPen(QColor(255, 255, 255, active ? 200 : 90), active ? 1.5 : 1));
		p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(255, 255, 255, active ? 230 : 150));
		p.drawRoundedRect(QRectF(x - 3, plot.top() - 2, 6, 12), 2, 2);
		if (active)
		{
			const QString text = formatFreq(edges[i + 1]) + " Hz";
			const double width = fm.horizontalAdvance(text);
			const double left = std::clamp(x + 6, plot.left(), plot.right() - width);
			p.setPen(modern::textColor());
			p.drawText(QPointF(left, plot.top() + 10 + fm.capHeight()), text);
		}
	}
}




void CrossoverEQDisplay::mousePressEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton) { return; }
	const QPointF pos = positionF(event);
	m_dragCrossover = crossoverAt(pos);
	if (m_dragCrossover < 0)
	{
		m_dragBand = bandAt(pos);
		if (m_dragBand >= 0)
		{
			m_dragStartY = pos.y();
			m_dragStartGain = gain(m_dragBand)->value();
			setCursor(Qt::SizeVerCursor);
		}
	}
	update();
}

void CrossoverEQDisplay::mouseMoveEvent(QMouseEvent* event)
{
	const QPointF pos = positionF(event);
	if (m_dragCrossover >= 0)
	{
		auto model = crossover(m_dragCrossover);
		model->setValue(static_cast<float>(std::clamp(toFreq(pos.x()), double(model->minValue()), double(model->maxValue()))));
	}
	else if (m_dragBand >= 0)
	{
		const double dbPerPixel = (MaxDb - MinDb) / plotRect().height();
		gain(m_dragBand)->setValue(static_cast<float>(m_dragStartGain + (m_dragStartY - pos.y()) * dbPerPixel));
	}
	else
	{
		const int hover = crossoverAt(pos);
		if (hover != m_hoverCrossover) { m_hoverCrossover = hover; update(); }
		setCursor(hover >= 0 ? Qt::SizeHorCursor : bandAt(pos) >= 0 ? Qt::SizeVerCursor : Qt::ArrowCursor);
	}
}

void CrossoverEQDisplay::mouseReleaseEvent(QMouseEvent*)
{
	m_dragCrossover = -1;
	m_dragBand = -1;
	update();
}

void CrossoverEQDisplay::mouseDoubleClickEvent(QMouseEvent* event)
{
	const QPointF pos = positionF(event);
	if (crossoverAt(pos) >= 0) { return; }
	if (const int band = bandAt(pos); band >= 0) { gain(band)->reset(); }
}


} // namespace lmms::gui
