/*
 * flangercontrolsdialog.cpp - defination of FlangerControlsDialog class.
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

#include "FlangerControlsDialog.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QVBoxLayout>
#include <complex>

#include "FlangerControls.h"
#include "FlangerEffect.h"
#include "FontHelper.h"
#include "ModernWidgets.h"
#include "TempoSyncKnob.h"

namespace lmms::gui
{

namespace
{
const QColor Accent = modern::accentColor();
}


FlangerControlsDialog::FlangerControlsDialog( FlangerControls *controls ) :
	EffectControlDialog( controls )
{
	modern::applyWindowStyle(this);

	auto root = new QVBoxLayout(this);
	root->setContentsMargins(8, 8, 8, 8);
	root->setSpacing(6);
	root->setSizeConstraint(QLayout::SetFixedSize);

	root->addWidget(new FlangerResponseDisplay(controls, this));

	auto row = new QHBoxLayout();
	row->setSpacing(6);

	auto sweep = new ModernSection(tr("Sweep"), this);
	sweep->grid()->addWidget(modern::makeKnob(this, &controls->m_delayTimeModel, tr("DELAY"),
		tr("Base delay:"), " s"), 0, 0);
	sweep->grid()->addWidget(modern::makeKnob(this, &controls->m_lfoAmountModel, tr("DEPTH"),
		tr("Sweep depth:"), " s"), 0, 1);
	auto rate = new TempoSyncKnob(KnobType::Modern, tr("RATE"), this);
	rate->setModel(&controls->m_lfoFrequencyModel);
	rate->setHintText(tr("LFO period:"), " s");
	sweep->grid()->addWidget(rate, 0, 2);
	sweep->grid()->addWidget(modern::makeKnob(this, &controls->m_lfoPhaseModel, tr("PHASE"),
		tr("Stereo phase:"), "°"), 0, 3);
	auto shape = new ModernSegmented(this);
	shape->setModel(&controls->m_shapeModel);
	sweep->grid()->addWidget(shape, 1, 0, 1, 4);
	row->addWidget(sweep);

	auto tone = new ModernSection(tr("Tone"), this);
	tone->grid()->addWidget(modern::makeKnob(this, &controls->m_feedbackModel, tr("FEEDBACK"),
		tr("Feedback:"), ""), 0, 0);
	tone->grid()->addWidget(modern::makeKnob(this, &controls->m_mixModel, tr("MIX"),
		tr("Dry/flanged mix:"), "%"), 0, 1);
	tone->grid()->addWidget(modern::makeKnob(this, &controls->m_whiteNoiseAmountModel, tr("NOISE"),
		tr("White noise amount:"), ""), 0, 2);
	auto cross = new ModernToggle(tr("CROSS L/R"), this);
	cross->setModel(&controls->m_invertFeedbackModel);
	cross->setToolTip(tr("Feed each channel's delay from the opposite input"));
	tone->grid()->addWidget(cross, 1, 0, 1, 3);
	row->addWidget(tone);

	root->addLayout(row);
}




FlangerResponseDisplay::FlangerResponseDisplay(FlangerControls* controls, QWidget* parent) :
	QWidget(parent),
	m_controls(controls)
{
	setMinimumSize(sizeHint());
	auto timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, qOverload<>(&QWidget::update));
	timer->start(33);
}




void FlangerResponseDisplay::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	const QRectF area = QRectF(rect());
	modern::paintDisplay(p, area, 0, 3);
	p.setRenderHint(QPainter::Antialiasing);

	constexpr double minFreq = 20.0, maxFreq = 20000.0;
	constexpr double minDb = -30.0, maxDb = 12.0;
	const QRectF plot = area.adjusted(6, 6, -6, -16);
	auto toX = [&](double f) { return plot.left() + plot.width() * std::log(f / minFreq) / std::log(maxFreq / minFreq); };
	auto toY = [&](double db) { return plot.bottom() - plot.height() * (std::clamp(db, minDb, maxDb) - minDb) / (maxDb - minDb); };

	p.setPen(QPen(modern::gridColor(), 1));
	for (double f : {100.0, 1000.0, 10000.0}) { p.drawLine(QPointF(toX(f), plot.top()), QPointF(toX(f), plot.bottom())); }
	p.setPen(QPen(QColor(255, 255, 255, 50), 1, Qt::DashLine));
	p.drawLine(QPointF(plot.left(), toY(0)), QPointF(plot.right(), toY(0)));

	const auto* effect = m_controls->m_effect;
	const double delay = std::max(1e-5f, effect->m_currentDelay.load(std::memory_order_relaxed));
	const double fb = m_controls->m_feedbackModel.value();
	const double mix = m_controls->m_mixModel.value() * 0.01;

	QPainterPath curve;
	QPainterPath fill(QPointF(plot.left(), plot.bottom()));
	const int steps = static_cast<int>(plot.width());
	for (int i = 0; i <= steps; ++i)
	{
		const double f = minFreq * std::pow(maxFreq / minFreq, static_cast<double>(i) / steps);
		const auto z = std::polar(1.0, -2.0 * std::numbers::pi * f * delay);
		const auto h = (1.0 - mix) + mix * z / (1.0 - fb * z);
		const double db = 20.0 * std::log10(std::max(std::abs(h), 1e-6));
		const QPointF pt(toX(f), toY(db));
		if (i == 0) { curve.moveTo(pt); } else { curve.lineTo(pt); }
		fill.lineTo(pt);
	}
	fill.lineTo(plot.right(), plot.bottom());
	fill.closeSubpath();

	QLinearGradient grad(0, plot.top(), 0, plot.bottom());
	QColor a = Accent;
	a.setAlpha(110);
	grad.setColorAt(0, a);
	a.setAlpha(10);
	grad.setColorAt(1, a);
	p.setPen(Qt::NoPen);
	p.setBrush(grad);
	p.drawPath(fill);
	p.setPen(QPen(Accent, 1.5));
	p.setBrush(Qt::NoBrush);
	p.drawPath(curve);

	p.setFont(adjustedToPixelSize(font(), SMALL_FONT_SIZE));
	p.setPen(modern::dimTextColor());
	p.drawText(QRectF(area.left() + 8, area.bottom() - 15, 200, 13), Qt::AlignLeft | Qt::AlignVCenter,
		tr("delay %1 ms  ·  comb spacing %2 Hz").arg(delay * 1000.0, 0, 'f', 2).arg(qRound(1.0 / delay)));
}


} // namespace lmms::gui
