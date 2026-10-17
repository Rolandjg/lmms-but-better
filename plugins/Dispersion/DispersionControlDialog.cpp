/*
 * DispersionControlDialog.cpp
 *
 * Copyright (c) 2023 Lost Robot <r94231/at/gmail/dot/com>
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


#include "DispersionControlDialog.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>
#include <complex>
#include <numbers>

#include "AudioEngine.h"
#include "DispersionControls.h"
#include "Engine.h"
#include "FontHelper.h"
#include "Knob.h"
#include "LcdSpinBox.h"
#include "ModernWidgets.h"


namespace lmms::gui
{

namespace
{
constexpr double MinFreq = 20.0;
constexpr double MaxFreq = 20000.0;
constexpr int DelayPoints = 256;

QString formatFreq(double hz)
{
	return hz >= 1000.0 ? QString("%1k").arg(hz / 1000.0, 0, 'f', hz >= 10000.0 ? 0 : 1) : QString::number(qRound(hz));
}

QString formatMs(double ms)
{
	return ms < 10.0 ? QString("%1 ms").arg(ms, 0, 'f', 1) : QString("%1 ms").arg(qRound(ms));
}
}


DispersionControlDialog::DispersionControlDialog(DispersionControls* controls) :
	EffectControlDialog(controls)
{
	modern::applyWindowStyle(this);

	auto root = new QVBoxLayout(this);
	root->setContentsMargins(8, 8, 8, 8);
	root->setSpacing(6);
	root->setSizeConstraint(QLayout::SetFixedSize);

	root->addWidget(new DispersionDisplay(controls, this));

	auto row = new QHBoxLayout();
	row->setSpacing(6);
	root->addLayout(row);

	auto filters = new ModernSection(tr("All-pass chain"), this);
	auto amountBox = new LcdSpinBox(3, this, "Amount");
	amountBox->setModel(&controls->m_amountModel);
	amountBox->setLabel(tr("AMOUNT"));
	amountBox->setToolTip(tr("Number of all-pass filters"));
	filters->grid()->addWidget(amountBox, 0, 0, Qt::AlignCenter);
	filters->grid()->addWidget(modern::makeKnob(this, &controls->m_freqModel, tr("FREQ"), tr("Frequency:"), " Hz"), 0, 1);
	filters->grid()->addWidget(modern::makeKnob(this, &controls->m_resoModel, tr("RESO"), tr("Resonance:"), ""), 0, 2);
	row->addWidget(filters);

	auto loop = new ModernSection(tr("Loop"), this);
	loop->grid()->addWidget(modern::makeKnob(this, &controls->m_feedbackModel, tr("FEEDBACK"), tr("Feedback:"), ""), 0, 0);
	auto dcButton = new ModernToggle(tr("DC FILTER"), this);
	dcButton->setModel(&controls->m_dcModel);
	dcButton->setToolTip(tr("Remove DC offset"));
	loop->grid()->addWidget(dcButton, 0, 1, Qt::AlignVCenter);
	row->addWidget(loop);
}




DispersionDisplay::DispersionDisplay(DispersionControls* controls, QWidget* parent) :
	QWidget(parent),
	m_controls(controls),
	m_recomputeTimer(new QTimer(this))
{
	setMinimumSize(sizeHint());
	m_recomputeTimer->setSingleShot(true);
	m_recomputeTimer->setInterval(40);
	connect(m_recomputeTimer, &QTimer::timeout, this, [this] { recompute(); update(); });
	for (Model* model : std::initializer_list<Model*>{&controls->m_amountModel, &controls->m_freqModel,
		&controls->m_resoModel, &controls->m_feedbackModel})
	{
		connect(model, &Model::dataChanged, m_recomputeTimer, qOverload<>(&QTimer::start));
	}
	recompute();
}




void DispersionDisplay::recompute()
{
	const double sampleRate = Engine::audioEngine()->outputSampleRate();
	const int amount = m_controls->m_amountModel.value();
	const double freq = m_controls->m_freqModel.value();
	const double reso = m_controls->m_resoModel.value();
	const double feedback = amount > 0 ? m_controls->m_feedbackModel.value() : 0.0;

	// Same coefficients as DispersionEffect: y = c1 (x - y2) + c2 (x1 - y1) + x2
	const double w0 = 2.0 * std::numbers::pi * freq / sampleRate;
	const double a0 = 1.0 + std::sin(w0) / (reso * 2.0);
	const double c1 = (1.0 - (a0 - 1.0)) / a0;
	const double c2 = -2.0 * std::cos(w0) / a0;

	// Whole effect: Y = H^N (X + fb z^-1 Y)
	auto response = [&](double w) {
		const auto z1 = std::polar(1.0, -w);
		const auto z2 = z1 * z1;
		const auto chain = std::pow((c1 + c2 * z1 + z2) / (1.0 + c2 * z1 + c1 * z2), amount);
		return chain / (1.0 - feedback * z1 * chain);
	};

	m_delayMs.assign(DelayPoints, 0.f);
	m_maxDelayMs = 0.f;
	m_peakFrequency = static_cast<float>(freq);
	if (amount > 0)
	{
		for (int i = 0; i < DelayPoints; ++i)
		{
			const double f = MinFreq * std::pow(MaxFreq / MinFreq, static_cast<double>(i) / (DelayPoints - 1));
			const double w = 2.0 * std::numbers::pi * std::min(f, sampleRate * 0.49) / sampleRate;
			const double dw = w * 1e-3;
			// Group delay = -d(phase)/dw, taken from the phase difference so no unwrapping is needed
			const double samples = -std::arg(response(w + dw) / response(w - dw)) / (2.0 * dw);
			m_delayMs[i] = static_cast<float>(std::max(0.0, samples) * 1000.0 / sampleRate);
			if (m_delayMs[i] > m_maxDelayMs) { m_maxDelayMs = m_delayMs[i]; m_peakFrequency = static_cast<float>(f); }
		}
	}

	// Impulse response, run through the same filter structure as the effect
	const int length = std::clamp(static_cast<int>(m_maxDelayMs * sampleRate / 1000.0 * 2.5) + 256, 512, 16384);
	m_impulse.assign(length, 0.f);
	std::vector<float> x0(amount), x1(amount), y0(amount), y1(amount);
	float feedbackValue = 0.f;
	for (int n = 0; n < length; ++n)
	{
		float s = (n == 0 ? 1.f : 0.f) + feedbackValue;
		for (int i = 0; i < amount; ++i)
		{
			const float out = static_cast<float>(c1 * (s - y1[i]) + c2 * (x0[i] - y0[i])) + x1[i];
			x1[i] = x0[i];
			x0[i] = s;
			y1[i] = y0[i];
			y0[i] = out;
			s = out;
		}
		feedbackValue = s * static_cast<float>(feedback);
		m_impulse[n] = s;
	}
}




void DispersionDisplay::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	const QRectF area = QRectF(rect());
	modern::paintDisplay(p, area, 0, 0);
	p.setRenderHint(QPainter::Antialiasing);
	p.setFont(adjustedToPixelSize(font(), SMALL_FONT_SIZE));
	const QFontMetricsF fm(p.font());

	const double sampleRate = Engine::audioEngine()->outputSampleRate();
	const int amount = m_controls->m_amountModel.value();
	const QRectF delayPlot(area.left() + 8, area.top() + 8, area.width() - 16, area.height() * 0.5 - 16);
	const QRectF impulsePlot(area.left() + 8, delayPlot.bottom() + 22, area.width() - 16, area.bottom() - delayPlot.bottom() - 30);

	auto toX = [&](double f) { return delayPlot.left() + delayPlot.width() * std::log(f / MinFreq) / std::log(MaxFreq / MinFreq); };

	// Delay per frequency
	p.setPen(QPen(modern::gridColor(), 1));
	for (double f : {50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0})
	{
		p.drawLine(QPointF(toX(f), delayPlot.top()), QPointF(toX(f), delayPlot.bottom()));
	}
	p.drawLine(delayPlot.bottomLeft(), delayPlot.bottomRight());
	p.setPen(modern::dimTextColor());
	for (double f : {100.0, 1000.0, 10000.0})
	{
		const QString text = formatFreq(f);
		p.drawText(QPointF(toX(f) + 3, delayPlot.bottom() - 3), text);
	}

	if (amount == 0 || m_maxDelayMs <= 0.f)
	{
		p.setPen(modern::dimTextColor());
		const QString text = tr("Turn up AMOUNT to add all-pass filters");
		p.drawText(QPointF(area.center().x() - fm.horizontalAdvance(text) / 2, area.center().y() + fm.capHeight() / 2), text);
		return;
	}

	const double scale = m_maxDelayMs * 1.15;
	QPainterPath curve;
	QPainterPath fill(delayPlot.bottomLeft());
	for (int i = 0; i < DelayPoints; ++i)
	{
		const double x = delayPlot.left() + delayPlot.width() * i / (DelayPoints - 1);
		const QPointF pt(x, delayPlot.bottom() - delayPlot.height() * std::min(1.0, m_delayMs[i] / scale));
		if (i == 0) { curve.moveTo(pt); } else { curve.lineTo(pt); }
		fill.lineTo(pt);
	}
	fill.lineTo(delayPlot.bottomRight());
	fill.closeSubpath();
	QLinearGradient grad(0, delayPlot.top(), 0, delayPlot.bottom());
	QColor a = modern::accentColor();
	a.setAlpha(110);
	grad.setColorAt(0, a);
	a.setAlpha(10);
	grad.setColorAt(1, a);
	p.setPen(Qt::NoPen);
	p.setBrush(grad);
	p.drawPath(fill);
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(modern::accentColor(), 1.6));
	p.drawPath(curve);

	p.setPen(modern::textColor());
	const QString delayText = tr("%1 Hz delayed %2").arg(formatFreq(m_peakFrequency)).arg(formatMs(m_maxDelayMs));
	p.drawText(QPointF(delayPlot.right() - fm.horizontalAdvance(delayText) - 2, delayPlot.top() + fm.capHeight() + 2), delayText);

	// Impulse response, drawn as min/max per pixel column
	p.setPen(QPen(modern::gridColor(), 1));
	p.drawLine(QPointF(impulsePlot.left(), impulsePlot.center().y()), QPointF(impulsePlot.right(), impulsePlot.center().y()));
	float peak = 1e-6f;
	for (float v : m_impulse) { peak = std::max(peak, std::abs(v)); }
	const int columns = static_cast<int>(impulsePlot.width());
	const double samplesPerColumn = static_cast<double>(m_impulse.size()) / columns;
	QColor wave = modern::secondaryColor();
	p.setPen(QPen(wave, 1));
	for (int c = 0; c < columns; ++c)
	{
		const auto begin = static_cast<std::size_t>(c * samplesPerColumn);
		const auto end = std::max(begin + 1, static_cast<std::size_t>((c + 1) * samplesPerColumn));
		float lo = 0.f, hi = 0.f;
		for (std::size_t n = begin; n < end && n < m_impulse.size(); ++n)
		{
			lo = std::min(lo, m_impulse[n]);
			hi = std::max(hi, m_impulse[n]);
		}
		// Square-root amplitude, so the quiet chirp tail is visible next to the initial click
		auto shape = [peak](float v) { return std::copysign(std::sqrt(std::abs(v) / peak), v); };
		const double x = impulsePlot.left() + c + 0.5;
		const double half = impulsePlot.height() * 0.5;
		p.drawLine(QPointF(x, impulsePlot.center().y() - shape(hi) * half), QPointF(x, impulsePlot.center().y() - shape(lo) * half));
	}

	p.setPen(modern::dimTextColor());
	p.drawText(QPointF(impulsePlot.left(), impulsePlot.top() - 6), tr("impulse response"));
	const QString lengthText = formatMs(m_impulse.size() * 1000.0 / sampleRate);
	p.drawText(QPointF(impulsePlot.right() - fm.horizontalAdvance(lengthText), impulsePlot.top() - 6), lengthText);
}


} // namespace lmms::gui
