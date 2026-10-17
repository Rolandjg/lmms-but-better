/*
 * LimiterControlDialog.cpp - editor for the lookahead brickwall limiter
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

#include "LimiterControlDialog.h"

#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>

#include "DeprecationHelper.h"
#include "FontHelper.h"
#include "Knob.h"
#include "LimiterControls.h"
#include "ModernWidgets.h"
#include "lmms_math.h"

namespace lmms::gui
{

namespace
{

constexpr float TopDb = 3.f;
constexpr float BottomDb = -33.f;
constexpr float MeterRangeDb = 12.f;

float toDb(float amp) { return amp > 1e-6f ? ampToDbfs(amp) : -120.f; }

QColor reductionColor() { return QColor(255, 92, 72); }

} // namespace


LimiterControlDialog::LimiterControlDialog(LimiterControls* controls) :
	EffectControlDialog(controls)
{
	modern::applyWindowStyle(this);

	auto root = new QVBoxLayout(this);
	root->setContentsMargins(8, 8, 8, 8);
	root->setSpacing(6);
	root->setSizeConstraint(QLayout::SetFixedSize);

	auto top = new QHBoxLayout();
	top->setSpacing(6);
	top->addWidget(new LimiterDisplay(controls, this), 1);
	auto reduction = new LimiterReductionMeter(controls, this);
	reduction->setToolTip(tr("Gain reduction (0 to -%1 dB)").arg(MeterRangeDb));
	top->addWidget(reduction);
	top->addWidget(new ModernMeter(this, &controls->m_outPeakL, &controls->m_outPeakR));
	root->addLayout(top);

	auto sections = new QHBoxLayout();
	sections->setSpacing(6);

	auto limiter = new ModernSection(tr("Limiter"), this);
	limiter->grid()->addWidget(modern::makeKnob(this, &controls->m_gainModel, tr("GAIN"),
		tr("Input gain:"), " dB"), 0, 0);
	limiter->grid()->addWidget(modern::makeKnob(this, &controls->m_ceilingModel, tr("CEILING"),
		tr("Output ceiling:"), " dB"), 0, 1);
	limiter->grid()->addWidget(modern::makeKnob(this, &controls->m_releaseModel, tr("RELEASE"),
		tr("Release:"), " ms"), 0, 2);
	sections->addWidget(limiter);

	auto detection = new ModernSection(tr("Detection"), this);
	detection->grid()->addWidget(modern::makeKnob(this, &controls->m_lookaheadModel, tr("LOOKAHEAD"),
		tr("Lookahead:"), " ms"), 0, 0);
	detection->grid()->addWidget(modern::makeKnob(this, &controls->m_linkModel, tr("LINK"),
		tr("Stereo link:"), "%"), 0, 1);
	auto truePeak = new ModernToggle(tr("TRUE PEAK"), this);
	truePeak->setModel(&controls->m_truePeakModel);
	truePeak->setToolTip(tr("Also catch peaks between samples (4x oversampled detection), so the ceiling holds "
		"after conversion to analog or lossy formats"));
	detection->grid()->addWidget(truePeak, 0, 2, Qt::AlignVCenter);
	sections->addWidget(detection);

	root->addLayout(sections);
}




LimiterDisplay::LimiterDisplay(LimiterControls* controls, QWidget* parent) :
	QWidget(parent),
	m_controls(controls)
{
	setMinimumSize(sizeHint());
	setMouseTracking(true);
	setCursor(Qt::SizeVerCursor);
	setToolTip(tr("Drag up to push the input gain; drag the ceiling line to move the ceiling"));

	auto timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, [this] { if (isVisible()) { update(); } });
	timer->start(33);
}




QRectF LimiterDisplay::plotRect() const
{
	return QRectF(rect()).adjusted(28, 8, -6, -8);
}




float LimiterDisplay::dbToY(float db, const QRectF& plot) const
{
	const float t = (TopDb - std::clamp(db, BottomDb, TopDb)) / (TopDb - BottomDb);
	return plot.top() + t * plot.height();
}




bool LimiterDisplay::nearCeiling(const QPointF& pos) const
{
	return std::abs(pos.y() - dbToY(m_controls->m_ceilingModel.value(), plotRect())) < 6;
}




void LimiterDisplay::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	const QRectF area = QRectF(rect());
	modern::paintDisplay(p, area, 0, 0);
	const QRectF plot = plotRect();

	// dB grid
	p.setFont(adjustedToPixelSize(font(), 9));
	for (float db = 0.f; db >= BottomDb; db -= 6.f)
	{
		const float y = dbToY(db, plot);
		p.setPen(modern::gridColor());
		p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
		p.setPen(modern::dimTextColor());
		p.drawText(QRectF(0, y - 7, plot.left() - 5, 14), Qt::AlignRight | Qt::AlignVCenter, QString::number(db));
	}

	// One pixel per history column, newest on the right
	const auto& c = *m_controls;
	const auto write = c.m_historyWrite.load(std::memory_order_acquire);
	const int columns = std::min(static_cast<int>(plot.width()), static_cast<int>(LimiterControls::HistorySize));
	QPainterPath input, output, reduction;
	input.moveTo(plot.right(), plot.bottom());
	output.moveTo(plot.right(), plot.bottom());
	reduction.moveTo(plot.right(), plot.top());
	float lowestGain = 1.f;
	for (int i = 0; i < columns; ++i)
	{
		const auto index = (write + LimiterControls::HistorySize - 1 - i) % LimiterControls::HistorySize;
		const float x = plot.right() - i;
		const float gain = c.m_historyGain[index].load(std::memory_order_relaxed);
		lowestGain = std::min(lowestGain, gain);
		input.lineTo(x, dbToY(toDb(c.m_historyIn[index].load(std::memory_order_relaxed)), plot));
		output.lineTo(x, dbToY(toDb(c.m_historyOut[index].load(std::memory_order_relaxed)), plot));
		// The reduction hangs from the top on the same dB scale
		reduction.lineTo(x, plot.top() + std::min(-toDb(gain), TopDb - BottomDb) / (TopDb - BottomDb) * plot.height());
	}
	const float left = plot.right() - columns + 1;
	input.lineTo(left, plot.bottom());
	output.lineTo(left, plot.bottom());
	reduction.lineTo(left, plot.top());

	p.setRenderHint(QPainter::Antialiasing);
	p.fillPath(input, QColor(140, 152, 170, 70));
	QLinearGradient outFill(plot.topLeft(), plot.bottomLeft());
	QColor outTop = modern::accentColor();
	outTop.setAlpha(150);
	QColor outBottom = modern::accentColor();
	outBottom.setAlpha(25);
	outFill.setColorAt(0, outTop);
	outFill.setColorAt(1, outBottom);
	p.fillPath(output, outFill);
	QColor red = reductionColor();
	red.setAlpha(170);
	p.fillPath(reduction, red);

	// Ceiling
	const float ceilingDb = c.m_ceilingModel.value();
	const float cy = dbToY(ceilingDb, plot);
	const bool ceilingActive = m_drag == Drag::Ceiling;
	QColor ceilingColor = modern::warningColor();
	ceilingColor.setAlpha(ceilingActive ? 255 : 190);
	p.setPen(QPen(ceilingColor, ceilingActive ? 1.6 : 1.1, Qt::DashLine));
	p.drawLine(QPointF(plot.left(), cy), QPointF(plot.right(), cy));
	p.setFont(adjustedToPixelSize(font(), 9));
	p.drawText(QRectF(plot.left(), cy - 15, plot.width() - 4, 13), Qt::AlignRight | Qt::AlignVCenter,
		tr("CEILING %1 dB").arg(ceilingDb, 0, 'f', 1));

	// Readout: current and largest visible reduction
	const float now = toDb(c.m_gain.load(std::memory_order_relaxed));
	m_displayGainDb = now < m_displayGainDb ? now : m_displayGainDb + (now - m_displayGainDb) * 0.25f;
	QFont big = adjustedToPixelSize(font(), 18);
	big.setBold(true);
	p.setFont(big);
	const QFontMetrics bigMetrics(big);
	const float textLeft = plot.left() + 8;
	const float baseline = plot.bottom() - 8;
	p.setPen(m_displayGainDb < -0.05f ? reductionColor().lighter(130) : modern::textColor());
	const QString gr = m_displayGainDb > -0.05f ? QStringLiteral("0.0") : QString::number(m_displayGainDb, 'f', 1);
	p.drawText(QPointF(textLeft, baseline), gr);
	p.setFont(adjustedToPixelSize(font(), 9));
	p.setPen(modern::dimTextColor());
	p.drawText(QPointF(textLeft + bigMetrics.horizontalAdvance(gr) + 6, baseline),
		tr("dB GR   ·   max %1 dB   ·   gain +%2 dB")
			.arg(lowestGain > 0.9943f ? 0.f : toDb(lowestGain), 0, 'f', 1)
			.arg(c.m_gainModel.value(), 0, 'f', 1));
}




void LimiterDisplay::mousePressEvent(QMouseEvent* me)
{
	if (me->button() != Qt::LeftButton) { return; }
	const auto pos = position(me);
	m_drag = nearCeiling(pos) ? Drag::Ceiling : Drag::Gain;
	m_dragStartY = pos.y();
	m_valueAtPress = m_drag == Drag::Ceiling ? m_controls->m_ceilingModel.value() : m_controls->m_gainModel.value();
	m_controls->m_gainModel.addJournalCheckPoint();
}




void LimiterDisplay::mouseMoveEvent(QMouseEvent* me)
{
	const auto pos = position(me);
	if (m_drag == Drag::None)
	{
		setCursor(nearCeiling(pos) ? Qt::SplitVCursor : Qt::SizeVerCursor);
		return;
	}

	const auto plot = plotRect();
	const float dbPerPixel = (TopDb - BottomDb) / plot.height() * (me->modifiers() & Qt::ShiftModifier ? 0.2f : 1.f);
	const float delta = (m_dragStartY - pos.y()) * dbPerPixel;
	if (m_drag == Drag::Ceiling) { m_controls->m_ceilingModel.setValue(m_valueAtPress + delta); }
	else { m_controls->m_gainModel.setValue(m_valueAtPress + delta); }
	update();
}




void LimiterDisplay::mouseReleaseEvent(QMouseEvent*)
{
	m_drag = Drag::None;
	update();
}




LimiterReductionMeter::LimiterReductionMeter(LimiterControls* controls, QWidget* parent) :
	QWidget(parent),
	m_controls(controls)
{
	setMinimumSize(sizeHint());
	auto timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, [this] {
		if (!isVisible()) { return; }
		const float reduction = std::clamp(-toDb(m_controls->m_gain.load(std::memory_order_relaxed)), 0.f, MeterRangeDb);
		m_display = reduction > m_display ? reduction : m_display + (reduction - m_display) * 0.2f;
		if (reduction >= m_hold) { m_hold = reduction; m_holdCounter = 45; }
		else if (--m_holdCounter <= 0) { m_hold = std::max(0.f, m_hold - 0.3f); }
		update();
	});
	timer->start(33);
}




void LimiterReductionMeter::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	const QRectF area = QRectF(rect());
	modern::paintDisplay(p, area, 0, 0);
	const QRectF bar = area.adjusted(3, 8, -3, -8);

	p.setPen(modern::gridColor());
	for (float db = 3.f; db < MeterRangeDb; db += 3.f)
	{
		const float y = bar.top() + db / MeterRangeDb * bar.height();
		p.drawLine(QPointF(bar.left(), y), QPointF(bar.right(), y));
	}

	const float h = m_display / MeterRangeDb * bar.height();
	QLinearGradient fill(bar.topLeft(), bar.bottomLeft());
	fill.setColorAt(0, reductionColor().lighter(115));
	fill.setColorAt(1, reductionColor().darker(150));
	p.fillRect(QRectF(bar.left(), bar.top(), bar.width(), h), fill);

	if (m_hold > 0.05f)
	{
		const float y = bar.top() + m_hold / MeterRangeDb * bar.height();
		p.setPen(QPen(reductionColor().lighter(150), 1.5));
		p.drawLine(QPointF(bar.left(), y), QPointF(bar.right(), y));
	}
}

} // namespace lmms::gui
