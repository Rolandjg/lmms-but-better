/*
 * PumperControlDialog.cpp - editor for the tempo-synced volume shaper
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

#include "PumperControlDialog.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QVBoxLayout>

#include "DeprecationHelper.h"
#include "Engine.h"
#include "AudioEngine.h"
#include "Knob.h"
#include "ModernWidgets.h"
#include "Pumper.h"
#include "PumperControls.h"
#include "Song.h"

namespace lmms::gui
{

PumperControlDialog::PumperControlDialog(PumperControls* controls) :
	EffectControlDialog(controls)
{
	modern::applyWindowStyle(this);

	auto root = new QVBoxLayout(this);
	root->setContentsMargins(8, 8, 8, 8);
	root->setSpacing(6);
	root->setSizeConstraint(QLayout::SetFixedSize);

	auto top = new QHBoxLayout();
	top->setSpacing(6);
	top->addWidget(new PumperShapeDisplay(controls, this), 1);
	top->addWidget(new ModernMeter(this, &controls->m_outPeakL, &controls->m_outPeakR));
	root->addLayout(top);

	auto rate = new ModernSegmented(this);
	rate->setModel(&controls->m_rateModel);
	rate->setToolTip(tr("Length of one pump cycle, locked to the song position"));
	root->addWidget(rate);

	auto sections = new QHBoxLayout();
	sections->setSpacing(6);

	auto shape = new ModernSection(tr("Shape"), this);
	shape->grid()->addWidget(modern::makeKnob(this, &controls->m_depthModel, tr("DEPTH"),
		tr("Depth:"), "%"), 0, 0);
	shape->grid()->addWidget(modern::makeKnob(this, &controls->m_attackModel, tr("ATTACK"),
		tr("Duck time:"), " ms"), 0, 1);
	shape->grid()->addWidget(modern::makeKnob(this, &controls->m_holdModel, tr("HOLD"),
		tr("Hold (share of the cycle):"), "%"), 0, 2);
	shape->grid()->addWidget(modern::makeKnob(this, &controls->m_releaseModel, tr("RELEASE"),
		tr("Release (share of the cycle):"), "%"), 0, 3);
	shape->grid()->addWidget(modern::makeKnob(this, &controls->m_curveModel, tr("CURVE"),
		tr("Release curve:"), "%"), 0, 4);
	shape->grid()->addWidget(modern::makeKnob(this, &controls->m_offsetModel, tr("OFFSET"),
		tr("Phase offset:"), "%"), 0, 5);
	sections->addWidget(shape);

	auto band = new ModernSection(tr("Band"), this);
	auto bandSelect = new ModernSegmented(this);
	bandSelect->setModel(&controls->m_bandModel);
	bandSelect->setToolTips({tr("Duck the whole signal"), tr("Duck only below the crossover - keeps the top end steady"),
		tr("Duck only above the crossover")});
	band->grid()->addWidget(bandSelect, 0, 0);
	band->grid()->addWidget(modern::makeKnob(this, &controls->m_crossoverModel, tr("XOVER"),
		tr("Crossover:"), " Hz"), 1, 0, Qt::AlignHCenter);
	sections->addWidget(band);

	root->addLayout(sections);
}




PumperShapeDisplay::PumperShapeDisplay(PumperControls* controls, QWidget* parent) :
	QWidget(parent),
	m_controls(controls)
{
	setMinimumSize(sizeHint());
	setCursor(Qt::SizeAllCursor);
	setToolTip(tr("Drag up/down to change the depth, left/right to change the release"));

	for (Model* m : std::initializer_list<Model*>{&controls->m_depthModel, &controls->m_attackModel,
		&controls->m_holdModel, &controls->m_releaseModel, &controls->m_curveModel, &controls->m_offsetModel,
		&controls->m_rateModel})
	{
		connect(m, &Model::dataChanged, this, qOverload<>(&QWidget::update));
	}

	// The playhead moves continuously
	auto timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, [this] { if (isVisible()) { update(); } });
	timer->start(33);
}




void PumperShapeDisplay::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	const QRectF area = QRectF(rect());
	modern::paintDisplay(p, area, 8, 4);
	p.setRenderHint(QPainter::Antialiasing);

	const QRectF plot = area.adjusted(8, 10, -8, -10);
	const float sr = Engine::audioEngine()->outputSampleRate();
	const float periodSeconds = m_controls->periodTicks() * Engine::framesPerTick() / sr;
	const float offset = m_controls->m_offsetModel.value() * 0.01f;

	// Two cycles read better than one: you see the shape and how it joins up
	constexpr int Cycles = 2;
	const int steps = static_cast<int>(plot.width());
	QPainterPath curve;
	QPainterPath fill;
	fill.moveTo(plot.left(), plot.bottom());
	for (int i = 0; i <= steps; ++i)
	{
		const float x = static_cast<float>(i) / steps;
		float phase = x * Cycles - offset;
		phase -= std::floor(phase);
		const float gain = m_controls->shapeAt(phase, periodSeconds);
		const QPointF pt(plot.left() + x * plot.width(), plot.bottom() - gain * plot.height());
		if (i == 0) { curve.moveTo(pt); } else { curve.lineTo(pt); }
		fill.lineTo(pt);
	}
	fill.lineTo(plot.right(), plot.bottom());
	fill.closeSubpath();

	QLinearGradient gradient(plot.topLeft(), plot.bottomLeft());
	QColor top = modern::accentColor();
	top.setAlpha(90);
	QColor bottom = modern::accentColor();
	bottom.setAlpha(10);
	gradient.setColorAt(0, top);
	gradient.setColorAt(1, bottom);
	p.fillPath(fill, gradient);
	p.setPen(QPen(modern::accentColor(), 2));
	p.drawPath(curve);

	// Beat markers at every cycle start
	p.setPen(QPen(QColor(255, 255, 255, 60), 1, Qt::DashLine));
	for (int cycle = 1; cycle < Cycles; ++cycle)
	{
		const qreal x = plot.left() + plot.width() * cycle / Cycles;
		p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
	}

	// Live playhead: phase is reported relative to the offset shape, map it back to the timeline
	const auto song = Engine::getSong();
	const bool running = song->isPlaying() || song->isExporting();
	float phase = m_controls->m_phase.load(std::memory_order_relaxed) + offset;
	phase -= std::floor(phase);
	const float gain = m_controls->m_gain.load(std::memory_order_relaxed);
	const qreal px = plot.left() + plot.width() * phase / Cycles;
	const qreal py = plot.bottom() - gain * plot.height();
	QColor head = running ? modern::textColor() : modern::dimTextColor();
	head.setAlpha(running ? 160 : 70);
	p.setPen(QPen(head, 1));
	p.drawLine(QPointF(px, plot.top()), QPointF(px, plot.bottom()));
	QRadialGradient glow(QPointF(px, py), 9);
	glow.setColorAt(0, QColor(255, 255, 255, running ? 200 : 60));
	glow.setColorAt(1, QColor(255, 255, 255, 0));
	p.setPen(Qt::NoPen);
	p.setBrush(glow);
	p.drawEllipse(QPointF(px, py), 9, 9);
	p.setBrush(modern::accentColor());
	p.drawEllipse(QPointF(px, py), 3, 3);

	// Readout
	QFont f = font();
	f.setPointSizeF(std::max(7.0, f.pointSizeF() - 1));
	p.setFont(f);
	p.setPen(modern::dimTextColor());
	const float depthDb = 20.f * std::log10(std::max(1.f - m_controls->m_depthModel.value() * 0.01f, 1e-4f));
	p.drawText(area.adjusted(10, 6, -10, -6), Qt::AlignBottom | Qt::AlignRight,
		tr("%1  ·  %2 dB").arg(m_controls->m_rateModel.currentText()).arg(depthDb, 0, 'f', 1));
}




void PumperShapeDisplay::mousePressEvent(QMouseEvent* me)
{
	m_dragStart = position(me);
	m_depthAtPress = m_controls->m_depthModel.value();
	m_releaseAtPress = m_controls->m_releaseModel.value();
}




void PumperShapeDisplay::mouseMoveEvent(QMouseEvent* me)
{
	if (!(me->buttons() & Qt::LeftButton)) { return; }
	const QPointF delta = position(me) - m_dragStart;
	m_controls->m_depthModel.setValue(m_depthAtPress + static_cast<float>(delta.y() / height() * 100.0));
	m_controls->m_releaseModel.setValue(m_releaseAtPress + static_cast<float>(delta.x() / width() * 100.0));
}

} // namespace lmms::gui
