/*
 * BassBoosterControlDialog.cpp - control dialog for bassbooster effect
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


#include "BassBoosterControlDialog.h"

#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <cmath>
#include <complex>
#include <numbers>

#include "AudioEngine.h"
#include "DeprecationHelper.h"
#include "BassBoosterControls.h"
#include "Engine.h"
#include "FontHelper.h"
#include "Knob.h"
#include "ModernWidgets.h"


namespace lmms::gui
{

namespace
{
constexpr double MinFreq = 20.0;
constexpr double MaxFreq = 20000.0;
constexpr double MinDb = -12.0;
constexpr double MaxDb = 24.0;

QString formatFreq(double hz)
{
	return hz >= 1000.0 ? QString("%1k").arg(hz / 1000.0, 0, 'f', 0) : QString::number(qRound(hz));
}
}


BassBoosterControlDialog::BassBoosterControlDialog( BassBoosterControls* controls ) :
	EffectControlDialog( controls )
{
	modern::applyWindowStyle(this);

	auto root = new QVBoxLayout(this);
	root->setContentsMargins(8, 8, 8, 8);
	root->setSpacing(6);
	root->setSizeConstraint(QLayout::SetFixedSize);

	auto top = new QHBoxLayout();
	top->setSpacing(4);
	top->addWidget(new BassBoosterDisplay(controls, this), 1);
	top->addWidget(new ModernMeter(this, &controls->m_outPeakL, &controls->m_outPeakR));
	root->addLayout(top);

	auto section = new ModernSection(tr("Bass boost"), this);
	section->grid()->addWidget(modern::makeKnob(this, &controls->m_freqModel, tr("FREQ"), tr("Frequency:"), " Hz"), 0, 0);
	section->grid()->addWidget(modern::makeKnob(this, &controls->m_ratioModel, tr("BOOST"), tr("Boost:"), "x"), 0, 1);
	section->grid()->addWidget(modern::makeKnob(this, &controls->m_gainModel, tr("GAIN"), tr("Output gain:"), "x"), 0, 2);
	root->addWidget(section);
}




BassBoosterDisplay::BassBoosterDisplay(BassBoosterControls* controls, QWidget* parent) :
	QWidget(parent),
	m_controls(controls)
{
	setMinimumSize(sizeHint());
	setMouseTracking(true);
	setCursor(Qt::PointingHandCursor);
	setToolTip(tr("Drag the handle: sideways sets the frequency, up and down the boost.\nDouble-click to reset."));
	for (auto model : {&controls->m_freqModel, &controls->m_gainModel, &controls->m_ratioModel})
	{
		connect(model, &FloatModel::dataChanged, this, qOverload<>(&QWidget::update));
	}
}




QRectF BassBoosterDisplay::plotRect() const
{
	return QRectF(rect()).adjusted(28, 8, -8, -20);
}

double BassBoosterDisplay::toX(double freq) const
{
	const QRectF plot = plotRect();
	return plot.left() + plot.width() * std::log(freq / MinFreq) / std::log(MaxFreq / MinFreq);
}

double BassBoosterDisplay::toFreq(double x) const
{
	const QRectF plot = plotRect();
	return MinFreq * std::pow(MaxFreq / MinFreq, (x - plot.left()) / plot.width());
}

double BassBoosterDisplay::toY(double db) const
{
	const QRectF plot = plotRect();
	return plot.bottom() - plot.height() * (std::clamp(db, MinDb, MaxDb) - MinDb) / (MaxDb - MinDb);
}

double BassBoosterDisplay::toDb(double y) const
{
	const QRectF plot = plotRect();
	return MinDb + (plot.bottom() - y) / plot.height() * (MaxDb - MinDb);
}

double BassBoosterDisplay::responseDb(double freq) const
{
	// Same filter as DspEffectLibrary::FastBassBoost: y = g * (x + r * lp(x)), with lp a one-pole
	// lowpass y1 = (x + y1 * c) / (c + 1)
	const double sampleRate = Engine::audioEngine()->outputSampleRate();
	const double c = BassBoosterControls::coefficientForCutoff(m_controls->m_freqModel.value(), sampleRate);
	const double a = c / (c + 1.0);
	const auto z1 = std::polar(1.0, -2.0 * std::numbers::pi * freq / sampleRate);
	const auto lowpass = (1.0 - a) / (1.0 - a * z1);
	const double gain = m_controls->m_gainModel.value();
	const double ratio = m_controls->m_ratioModel.value();
	const auto h = gain * (1.0 + ratio * lowpass);
	return 20.0 * std::log10(std::max(std::abs(h), 1e-6));
}

QPointF BassBoosterDisplay::handlePos() const
{
	// The handle sits on the low-end plateau of the curve
	const double boostDb = 20.0 * std::log10(m_controls->m_gainModel.value() * (1.0 + m_controls->m_ratioModel.value()));
	return QPointF(toX(m_controls->m_freqModel.value()), toY(boostDb));
}




void BassBoosterDisplay::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	const QRectF area = QRectF(rect());
	const QRectF plot = plotRect();
	modern::paintDisplay(p, area, 0, 0);
	p.setRenderHint(QPainter::Antialiasing);
	p.setFont(adjustedToPixelSize(font(), SMALL_FONT_SIZE));
	const QFontMetricsF fm(p.font());

	// Grid with frequency and level labels
	p.setPen(QPen(modern::gridColor(), 1));
	for (double f : {50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0})
	{
		p.drawLine(QPointF(toX(f), plot.top()), QPointF(toX(f), plot.bottom()));
	}
	for (double db : {-6.0, 6.0, 12.0, 18.0})
	{
		p.drawLine(QPointF(plot.left(), toY(db)), QPointF(plot.right(), toY(db)));
	}
	p.setPen(QPen(QColor(255, 255, 255, 50), 1, Qt::DashLine));
	p.drawLine(QPointF(plot.left(), toY(0)), QPointF(plot.right(), toY(0)));

	p.setPen(modern::dimTextColor());
	for (double db : {0.0, 12.0})
	{
		const QString text = db > 0 ? QString("+%1").arg(db) : QString::number(db);
		p.drawText(QPointF(plot.left() - 5 - fm.horizontalAdvance(text), toY(db) + fm.capHeight() / 2), text);
	}
	for (double f : {100.0, 1000.0, 10000.0})
	{
		const QString text = formatFreq(f);
		p.drawText(QPointF(toX(f) - fm.horizontalAdvance(text) / 2, plot.bottom() + 4 + fm.capHeight()), text);
	}

	// The response, filled where it boosts
	QPainterPath curve;
	QPainterPath fill(QPointF(plot.left(), toY(0)));
	const int steps = static_cast<int>(plot.width());
	for (int i = 0; i <= steps; ++i)
	{
		const double x = plot.left() + i;
		const QPointF pt(x, toY(responseDb(toFreq(x))));
		if (i == 0) { curve.moveTo(pt); } else { curve.lineTo(pt); }
		fill.lineTo(pt);
	}
	fill.lineTo(plot.right(), toY(0));
	fill.closeSubpath();

	QLinearGradient grad(0, plot.top(), 0, toY(0));
	QColor a = modern::accentColor();
	a.setAlpha(120);
	grad.setColorAt(0, a);
	a.setAlpha(15);
	grad.setColorAt(1, a);
	p.setPen(Qt::NoPen);
	p.setBrush(grad);
	p.drawPath(fill);
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(modern::accentColor(), 1.8));
	p.drawPath(curve);

	// Corner marker and handle
	const QPointF handle = handlePos();
	p.setPen(QPen(QColor(255, 255, 255, 40), 1, Qt::DotLine));
	p.drawLine(QPointF(handle.x(), plot.top()), QPointF(handle.x(), plot.bottom()));
	const double radius = m_dragging || m_hover ? 6.0 : 5.0;
	QColor glow = modern::accentColor();
	glow.setAlpha(m_dragging ? 90 : 50);
	p.setPen(Qt::NoPen);
	p.setBrush(glow);
	p.drawEllipse(handle, radius + 4, radius + 4);
	p.setBrush(modern::accentColor());
	p.setPen(QPen(modern::displayColor(), 1.5));
	p.drawEllipse(handle, radius, radius);

	// Readout
	const double boostDb = responseDb(MinFreq) - responseDb(MaxFreq / 2);
	const QString readout = tr("+%1 dB below %2 Hz").arg(boostDb, 0, 'f', 1).arg(qRound(m_controls->m_freqModel.value()));
	const double width = fm.horizontalAdvance(readout);
	p.setPen(modern::textColor());
	p.drawText(QPointF(plot.right() - width - 4, plot.top() + 4 + fm.capHeight()), readout);
}




void BassBoosterDisplay::mousePressEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton) { return; }
	m_dragging = true;
	mouseMoveEvent(event);
}

void BassBoosterDisplay::mouseMoveEvent(QMouseEvent* event)
{
	const QPointF pos = positionF(event);
	if (!m_dragging)
	{
		const bool hover = QLineF(pos, handlePos()).length() < 12.0;
		if (hover != m_hover) { m_hover = hover; update(); }
		return;
	}
	auto& freq = m_controls->m_freqModel;
	auto& ratio = m_controls->m_ratioModel;
	freq.setValue(static_cast<float>(std::clamp(toFreq(pos.x()), double(freq.minValue()), double(freq.maxValue()))));
	const double plateau = std::pow(10.0, toDb(pos.y()) / 20.0) / m_controls->m_gainModel.value();
	ratio.setValue(static_cast<float>(std::max(plateau - 1.0, 0.0)));
}

void BassBoosterDisplay::mouseReleaseEvent(QMouseEvent*)
{
	m_dragging = false;
	update();
}

void BassBoosterDisplay::mouseDoubleClickEvent(QMouseEvent*)
{
	m_controls->m_freqModel.reset();
	m_controls->m_ratioModel.reset();
}


} // namespace lmms::gui
