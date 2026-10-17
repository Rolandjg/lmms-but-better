/*
 * BitcrushControlDialog.cpp - A native bitcrusher
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

#include "BitcrushControlDialog.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QVBoxLayout>
#include <array>
#include <cmath>
#include <numbers>

#include "AudioEngine.h"
#include "Bitcrush.h"
#include "BitcrushControls.h"
#include "Engine.h"
#include "FontHelper.h"
#include "Knob.h"
#include "ModernWidgets.h"

namespace lmms::gui
{


BitcrushControlDialog::BitcrushControlDialog( BitcrushControls * controls ) :
	EffectControlDialog( controls )
{
	modern::applyWindowStyle(this);

	auto root = new QVBoxLayout(this);
	root->setContentsMargins(8, 8, 8, 8);
	root->setSpacing(6);
	root->setSizeConstraint(QLayout::SetFixedSize);

	auto top = new QHBoxLayout();
	top->setSpacing(4);
	top->addWidget(new BitcrushDisplay(controls, this), 1);
	top->addWidget(new ModernMeter(this, &controls->m_outPeakL, &controls->m_outPeakR));
	root->addLayout(top);

	auto layout = new QHBoxLayout();
	layout->setSpacing(6);
	root->addLayout(layout);

	auto input = new ModernSection(tr("Input"), this);
	input->grid()->addWidget(modern::makeKnob(this, &controls->m_inGain, tr("GAIN"), tr("Input gain:"), " dBFS"), 0, 0);
	input->grid()->addWidget(modern::makeKnob(this, &controls->m_inNoise, tr("NOISE"), tr("Input noise:"), "%"), 0, 1);
	layout->addWidget(input);

	auto rateSection = new ModernSection(tr("Rate"), this);
	auto rateEnabled = new ModernToggle(tr("ON"), this);
	rateEnabled->setModel(&controls->m_rateEnabled);
	rateEnabled->setToolTip(tr("Enable sample-rate crushing"));
	rateSection->grid()->addWidget(modern::makeKnob(this, &controls->m_rate, tr("FREQ"), tr("Sample rate:"), " Hz"), 0, 0);
	rateSection->grid()->addWidget(modern::makeKnob(this, &controls->m_stereoDiff, tr("STEREO"), tr("Stereo difference:"), "%"), 0, 1);
	rateSection->grid()->addWidget(rateEnabled, 1, 0, 1, 2);
	layout->addWidget(rateSection);

	auto depthSection = new ModernSection(tr("Depth"), this);
	auto depthEnabled = new ModernToggle(tr("ON"), this);
	depthEnabled->setModel(&controls->m_depthEnabled);
	depthEnabled->setToolTip(tr("Enable bit-depth crushing"));
	depthSection->grid()->addWidget(modern::makeKnob(this, &controls->m_levels, tr("LEVELS"), tr("Levels:"), ""), 0, 0);
	depthSection->grid()->addWidget(depthEnabled, 1, 0);
	layout->addWidget(depthSection);

	auto output = new ModernSection(tr("Output"), this);
	output->grid()->addWidget(modern::makeKnob(this, &controls->m_outGain, tr("GAIN"), tr("Output gain:"), " dBFS"), 0, 0);
	output->grid()->addWidget(modern::makeKnob(this, &controls->m_outClip, tr("CLIP"), tr("Output clip:"), " dBFS"), 0, 1);
	layout->addWidget(output);
}




BitcrushDisplay::BitcrushDisplay(BitcrushControls* controls, QWidget* parent) :
	QWidget(parent),
	m_controls(controls)
{
	setMinimumSize(sizeHint());
	auto timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, qOverload<>(&QWidget::update));
	timer->start(33);
}




void BitcrushDisplay::paintEvent(QPaintEvent*)
{
	// Same oversampling factor as the effect, so the hold steps land where the DSP puts them
	constexpr int Oversampling = 5;
	constexpr int Window = 256;

	QPainter p(this);
	const QRectF area = QRectF(rect());
	modern::paintDisplay(p, area, 0, 0);
	p.setRenderHint(QPainter::Antialiasing);

	// Grab the newest input, triggered on a rising zero crossing so periodic signals stand still
	const ScopeBuffer& scope = m_controls->m_effect->m_scope;
	const std::size_t end = scope.writePosition() + ScopeBuffer::Size;
	std::size_t start = end - Window;
	for (std::size_t i = end - Window; i > end - ScopeBuffer::Size + 1; --i)
	{
		if (scope.left(i - 1) <= 0.f && scope.left(i) > 0.f) { start = i; break; }
	}

	std::array<float, Window> input{};
	float peak = 0.f;
	for (int i = 0; i < Window; ++i)
	{
		input[i] = scope.left(start + i);
		peak = std::max(peak, std::abs(input[i]));
	}

	// Without signal, show what the settings do to a sine
	const bool preview = peak < 1e-4f;
	if (preview)
	{
		for (int i = 0; i < Window; ++i)
		{
			input[i] = 0.8f * std::sin(2.f * std::numbers::pi_v<float> * 2.5f * i / Window);
		}
		peak = 0.8f;
	}

	// Zoom in on quiet signals in octave steps, easing so the view doesn't jump around
	const float targetZoom = std::clamp(std::exp2(std::floor(std::log2(0.9f / std::max(peak, 1e-3f)))), 1.f, 16.f);
	m_zoom = targetZoom < m_zoom ? targetZoom : m_zoom + (targetZoom - m_zoom) * 0.15f;

	const QRectF plot = area.adjusted(6, 6, -6, -18);
	const double midY = plot.center().y();
	auto toX = [&](double sample) { return plot.left() + plot.width() * sample / (Window - 1); };
	auto toY = [&](double v) { return midY - std::clamp(v * m_zoom, -1.0, 1.0) * plot.height() * 0.5; };

	const bool depthOn = m_controls->m_depthEnabled.value();
	const bool rateOn = m_controls->m_rateEnabled.value();
	const float levels = std::max(1.f, m_controls->m_levels.value());
	auto crush = [&](float v) { return depthOn ? std::round(v * levels) / levels : v; };

	// Quantization steps, when they are coarse enough to see
	const double stepPx = plot.height() * 0.5 * m_zoom / levels;
	if (depthOn && stepPx >= 4.0)
	{
		p.setPen(QPen(QColor(255, 255, 255, 14), 1));
		for (double y = midY - stepPx; y > plot.top(); y -= stepPx) { p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y)); }
		for (double y = midY + stepPx; y < plot.bottom(); y += stepPx) { p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y)); }
	}
	p.setPen(QPen(modern::gridColor(), 1));
	p.drawLine(QPointF(plot.left(), midY), QPointF(plot.right(), midY));

	// Clean input
	QPainterPath clean;
	for (int i = 0; i < Window; ++i)
	{
		const QPointF pt(toX(i), toY(input[i]));
		if (i == 0) { clean.moveTo(pt); } else { clean.lineTo(pt); }
	}
	p.setPen(QPen(QColor(255, 255, 255, 70), 1.2));
	p.drawPath(clean);

	// Crushed signal: the effect's sample-and-hold, run on the same samples
	const float sampleRate = Engine::audioEngine()->outputSampleRate();
	const float rate = m_controls->m_rate.value();
	const float holdLength = rateOn ? sampleRate * Oversampling / std::max(1.f, rate) : 0.f;
	float counter = holdLength;
	float held = crush(input[0]);
	QPainterPath crushed;
	QPainterPath fill;
	crushed.moveTo(toX(0), toY(held));
	fill.moveTo(toX(0), midY);
	fill.lineTo(toX(0), toY(held));
	for (int i = 0; i < Window; ++i)
	{
		for (int o = 0; o < Oversampling; ++o)
		{
			const double x = toX(i + static_cast<double>(o) / Oversampling);
			float next = held;
			if (!rateOn) { next = crush(input[i]); }
			else if ((counter += 1.f) > holdLength)
			{
				counter -= holdLength;
				next = crush(input[i]);
			}
			if (next != held)
			{
				crushed.lineTo(x, toY(held));
				crushed.lineTo(x, toY(next));
				fill.lineTo(x, toY(held));
				fill.lineTo(x, toY(next));
				held = next;
			}
		}
	}
	crushed.lineTo(plot.right(), toY(held));
	fill.lineTo(plot.right(), toY(held));
	fill.lineTo(plot.right(), midY);
	fill.closeSubpath();

	QColor fillColor = modern::accentColor();
	fillColor.setAlpha(40);
	p.setPen(Qt::NoPen);
	p.setBrush(fillColor);
	p.drawPath(fill);
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(modern::accentColor(), 1.6, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
	p.drawPath(crushed);

	// Readouts
	p.setFont(adjustedToPixelSize(font(), SMALL_FONT_SIZE));
	const QFontMetricsF fm(p.font());
	const double baseline = area.bottom() - 5;
	const double ratio = rateOn ? sampleRate / std::max(1.f, rate) : 1.0;
	const QString rateText = rateOn
		? tr("%1 Hz").arg(rate >= 1000.f ? QString::number(rate / 1000.f, 'f', 1) + "k" : QString::number(qRound(rate)))
			+ (ratio > 1.05 ? tr("  (÷%1)").arg(ratio, 0, 'f', ratio < 10 ? 1 : 0) : QString())
		: tr("rate off");
	const QString depthText = depthOn
		? tr("%1 levels ≈ %2 bit").arg(qRound(levels)).arg(std::log2(2.0 * levels + 1.0), 0, 'f', 1)
		: tr("depth off");
	p.setPen(modern::dimTextColor());
	p.drawText(QPointF(area.left() + 8, baseline), rateText + "  ·  " + depthText);

	QStringList tags;
	if (preview) { tags << tr("preview"); }
	if (m_zoom > 1.05f) { tags << tr("zoom ×%1").arg(qRound(m_zoom)); }
	const QString tagText = tags.join("  ·  ");
	p.drawText(QPointF(area.right() - 8 - fm.horizontalAdvance(tagText), baseline), tagText);
}


} // namespace lmms::gui
