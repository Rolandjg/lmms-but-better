/*
 * LoudnessMeterDialog.cpp - display of the loudness meter
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

#include "LoudnessMeterDialog.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>

#include "LoudnessAnalyzer.h"
#include "LoudnessMeterControls.h"
#include "ModernWidgets.h"

namespace lmms::gui
{

namespace
{

constexpr float GraphTop = 0.f;     // LUFS at the top of the graph
constexpr float GraphBottom = -48.f;

QColor dangerColor() { return QColor(240, 84, 84); }

//! Color for a loudness compared to the target: on target is green, too loud warns
QColor colorForDelta(float delta)
{
	if (!std::isfinite(delta)) { return modern::dimTextColor(); }
	if (delta > 3.f) { return dangerColor(); }
	if (delta > 1.f) { return modern::warningColor(); }
	if (delta >= -1.f) { return modern::accentColor(); }
	return modern::secondaryColor();
}

QString formatLufs(float lufs)
{
	return std::isfinite(lufs) && lufs > -70.f ? QString::number(lufs, 'f', 1) : QString::fromUtf8("–∞");
}

}


LoudnessMeterDialog::LoudnessMeterDialog(LoudnessMeterControls* controls) :
	EffectControlDialog(controls)
{
	modern::applyWindowStyle(this);

	auto root = new QVBoxLayout(this);
	root->setContentsMargins(8, 8, 8, 8);
	root->setSpacing(6);
	root->setSizeConstraint(QLayout::SetFixedSize);

	root->addWidget(new LoudnessDisplay(controls, this));

	auto bottom = new QHBoxLayout();
	bottom->setSpacing(6);
	auto target = new ModernSegmented(this);
	target->setModel(&controls->m_targetModel);
	target->setToolTip(tr("Delivery target: sets the reference line and the true-peak ceiling"));
	bottom->addWidget(target, 1);

	auto autoReset = new ModernToggle(tr("AUTO RESET"), this);
	autoReset->setModel(&controls->m_resetOnPlayModel);
	autoReset->setToolTip(tr("Start a fresh measurement every time playback starts"));
	bottom->addWidget(autoReset);

	auto reset = new QPushButton(tr("RESET"), this);
	reset->setCursor(Qt::PointingHandCursor);
	reset->setToolTip(tr("Clear integrated loudness, range and true peak (or double-click the display)"));
	reset->setStyleSheet(QString("QPushButton { background: %1; color: %2; border: none; border-radius: 3px;"
		" padding: 3px 10px; font-size: 8pt; font-weight: bold; } QPushButton:hover { background: %3; }")
		.arg(modern::buttonColor().name(), modern::textColor().name(), modern::buttonColor().lighter(125).name()));
	connect(reset, &QPushButton::clicked, this, [controls] { controls->m_resetRequested = true; });
	bottom->addWidget(reset);
	root->addLayout(bottom);
}




LoudnessDisplay::LoudnessDisplay(LoudnessMeterControls* controls, QWidget* parent) :
	QWidget(parent),
	m_controls(controls)
{
	setMinimumSize(sizeHint());
	connect(&controls->m_targetModel, &Model::dataChanged, this, qOverload<>(&QWidget::update));
	auto timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, [this] { if (isVisible()) { update(); } });
	timer->start(40);
}




void LoudnessDisplay::mouseDoubleClickEvent(QMouseEvent*)
{
	m_controls->m_resetRequested = true;
}




void LoudnessDisplay::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	const QRectF area = QRectF(rect());

	const float target = m_controls->targetLufs();
	const float ceiling = m_controls->ceilingDb();
	const float momentary = m_controls->m_momentary.load(std::memory_order_relaxed);
	const float shortTerm = m_controls->m_shortTerm.load(std::memory_order_relaxed);
	const float integrated = m_controls->m_integrated.load(std::memory_order_relaxed);
	const float range = m_controls->m_range.load(std::memory_order_relaxed);
	const float truePeakLinear = m_controls->m_truePeak.load(std::memory_order_relaxed);
	const float truePeak = truePeakLinear > 0.f ? 20.f * std::log10(truePeakLinear) : -std::numeric_limits<float>::infinity();

	// --- Layout: graph + bar on top, readout tiles below
	const qreal readoutHeight = 92;
	const QRectF graphArea(area.left(), area.top(), area.width() - 40, area.height() - readoutHeight - 6);
	const QRectF barArea(graphArea.right() + 6, area.top(), 34, graphArea.height());
	const QRectF readoutArea(area.left(), graphArea.bottom() + 6, area.width(), readoutHeight);

	modern::paintDisplay(p, graphArea, 0, 0);
	const QRectF plot = graphArea.adjusted(30, 8, -6, -8);
	const auto yFor = [&](float lufs)
	{
		const float v = std::clamp((lufs - GraphBottom) / (GraphTop - GraphBottom), 0.f, 1.f);
		return plot.bottom() - v * plot.height();
	};

	QFont small = font();
	small.setPointSizeF(std::max(6.5, small.pointSizeF() - 1.5));
	p.setFont(small);

	// Scale
	for (int lufs = static_cast<int>(GraphTop); lufs >= GraphBottom; lufs -= 6)
	{
		const qreal y = yFor(static_cast<float>(lufs));
		p.setPen(modern::gridColor());
		p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
		p.setPen(modern::dimTextColor());
		p.drawText(QRectF(graphArea.left(), y - 7, 26, 14), Qt::AlignRight | Qt::AlignVCenter, QString::number(lufs));
	}

	// Target corridor (±1 LU) and line
	QColor corridor = modern::accentColor();
	corridor.setAlpha(28);
	p.fillRect(QRectF(QPointF(plot.left(), yFor(target + 1.f)), QPointF(plot.right(), yFor(target - 1.f))), corridor);
	p.setPen(QPen(modern::accentColor(), 1, Qt::DashLine));
	p.drawLine(QPointF(plot.left(), yFor(target)), QPointF(plot.right(), yFor(target)));
	p.setPen(modern::accentColor());
	p.drawText(QRectF(plot.left() + 4, yFor(target) - 15, 160, 14), Qt::AlignLeft | Qt::AlignVCenter,
		tr("target %1 LUFS").arg(target, 0, 'f', 0));

	// History: short-term as a filled area, momentary as a thin line
	constexpr std::size_t Size = LoudnessMeterControls::HistorySize;
	const std::size_t write = m_controls->m_historyWrite.load(std::memory_order_acquire);
	const auto xFor = [&](std::size_t age) { return plot.right() - plot.width() * age / (Size - 1); };
	QPainterPath shortPath;
	QPainterPath shortFill;
	QPainterPath momentaryPath;
	bool shortOpen = false;
	bool momentaryOpen = false;
	qreal fillStart = 0;
	const auto closeFill = [&](qreal x)
	{
		shortFill.lineTo(x, plot.bottom());
		shortFill.lineTo(fillStart, plot.bottom());
		shortFill.closeSubpath();
	};
	qreal lastX = plot.right();
	for (std::size_t age = 0; age < Size; ++age)
	{
		const std::size_t i = (write + Size - 1 - age) % Size;
		const float st = m_controls->m_shortTermHistory[i].load(std::memory_order_relaxed);
		const float mo = m_controls->m_momentaryHistory[i].load(std::memory_order_relaxed);
		const qreal x = xFor(age);
		// Silence leaves a gap instead of a line along the floor
		if (std::isfinite(st) && st > GraphBottom)
		{
			const QPointF sp(x, yFor(st));
			if (!shortOpen)
			{
				shortPath.moveTo(sp);
				fillStart = x;
				shortFill.moveTo(x, plot.bottom());
				shortFill.lineTo(sp);
				shortOpen = true;
			}
			else
			{
				shortPath.lineTo(sp);
				shortFill.lineTo(sp);
			}
		}
		else if (shortOpen)
		{
			closeFill(lastX);
			shortOpen = false;
		}
		if (std::isfinite(mo) && mo > GraphBottom)
		{
			const QPointF mp(x, yFor(mo));
			if (!momentaryOpen) { momentaryPath.moveTo(mp); momentaryOpen = true; }
			else { momentaryPath.lineTo(mp); }
		}
		else { momentaryOpen = false; }
		lastX = x;
	}
	if (shortOpen) { closeFill(lastX); }
	QLinearGradient fillGradient(plot.topLeft(), plot.bottomLeft());
	QColor fillTop = modern::secondaryColor();
	fillTop.setAlpha(110);
	QColor fillBottom = modern::secondaryColor();
	fillBottom.setAlpha(8);
	fillGradient.setColorAt(0, fillTop);
	fillGradient.setColorAt(1, fillBottom);
	p.save();
	p.setClipRect(plot);
	p.fillPath(shortFill, fillGradient);
	p.setPen(QPen(modern::secondaryColor(), 1.5));
	p.drawPath(shortPath);
	p.setPen(QPen(QColor(255, 255, 255, 110), 1));
	p.drawPath(momentaryPath);
	p.restore();

	p.setPen(modern::dimTextColor());
	p.drawText(plot.adjusted(4, 0, 0, -2), Qt::AlignLeft | Qt::AlignBottom, tr("-60 s"));
	p.drawText(plot.adjusted(0, 0, -4, -2), Qt::AlignRight | Qt::AlignBottom, tr("now"));

	// --- Momentary bar with peak hold, colored against the target
	modern::paintDisplay(p, barArea, 0, 0);
	const QRectF bar = barArea.adjusted(9, 8, -9, -8);
	const auto barY = [&](float lufs)
	{
		const float v = std::clamp((lufs - GraphBottom) / (GraphTop - GraphBottom), 0.f, 1.f);
		return bar.bottom() - v * bar.height();
	};
	const float shown = std::isfinite(momentary) ? momentary : -70.f;
	m_barDisplay = shown > m_barDisplay ? shown : std::max(shown, m_barDisplay - 1.2f);
	if (shown >= m_peakHold) { m_peakHold = shown; m_peakHoldTicks = 40; }
	else if (--m_peakHoldTicks < 0) { m_peakHold = std::max(shown, m_peakHold - 0.6f); }
	QLinearGradient barGradient(bar.bottomLeft(), bar.topLeft());
	const auto stop = [&](float lufs) { return std::clamp((lufs - GraphBottom) / (GraphTop - GraphBottom), 0.f, 1.f); };
	barGradient.setColorAt(0, modern::secondaryColor());
	barGradient.setColorAt(stop(target - 1.5f), modern::secondaryColor());
	barGradient.setColorAt(stop(target), modern::accentColor());
	barGradient.setColorAt(stop(target + 2.f), modern::warningColor());
	barGradient.setColorAt(std::min(1.f, stop(target + 4.f)), dangerColor());
	barGradient.setColorAt(1, dangerColor());
	p.fillRect(QRectF(bar.left(), bar.top(), bar.width(), bar.height()), QColor(255, 255, 255, 10));
	p.fillRect(QRectF(QPointF(bar.left(), barY(m_barDisplay)), bar.bottomRight()), barGradient);
	p.fillRect(QRectF(bar.left() - 2, barY(m_peakHold) - 1, bar.width() + 4, 2), modern::textColor());
	p.setPen(QPen(modern::textColor(), 1, Qt::DashLine));
	p.drawLine(QPointF(barArea.left() + 2, barY(target)), QPointF(barArea.right() - 2, barY(target)));

	// --- Readouts
	const qreal bigWidth = readoutArea.width() * 0.36;
	const QRectF bigTile(readoutArea.left(), readoutArea.top(), bigWidth, readoutArea.height());
	const auto paintTile = [&](const QRectF& r)
	{
		QPainterPath path;
		path.addRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
		p.fillPath(path, modern::sectionColor());
		p.setPen(modern::sectionBorder());
		p.drawPath(path);
	};

	paintTile(bigTile);
	QFont caption = small;
	caption.setBold(true);
	caption.setLetterSpacing(QFont::AbsoluteSpacing, 1);
	const QRectF bigInner = bigTile.adjusted(12, 9, -12, -9);
	const QFontMetricsF captionFm(caption);
	p.setFont(caption);
	p.setPen(modern::dimTextColor());
	p.drawText(QPointF(bigInner.left(), bigInner.top() + captionFm.capHeight()), tr("INTEGRATED"));
	p.drawText(QPointF(bigInner.left(), bigInner.bottom()), tr("LUFS"));
	QFont big = font();
	big.setPixelSize(38);
	big.setBold(true);
	const QFontMetricsF bigFm(big);
	p.setFont(big);
	const QColor integratedColor = colorForDelta(integrated - target);
	p.setPen(integratedColor);
	p.drawText(QPointF(bigInner.left() - 2, bigInner.center().y() + bigFm.capHeight() / 2), formatLufs(integrated));
	if (std::isfinite(integrated) && integrated > -70.f)
	{
		const float delta = integrated - target;
		const QString text = QString("%1%2 LU").arg(delta >= 0 ? "+" : "").arg(delta, 0, 'f', 1);
		p.setFont(caption);
		p.drawText(QPointF(bigInner.right() - captionFm.horizontalAdvance(text), bigInner.bottom()), text);
	}

	struct Readout { QString caption; QString value; QString unit; QColor color; };
	const bool overCeiling = std::isfinite(truePeak) && truePeak > ceiling;
	const std::array<Readout, 4> readouts{{
		{tr("SHORT-TERM"), formatLufs(shortTerm), tr("LUFS"), colorForDelta(shortTerm - target)},
		{tr("MOMENTARY"), formatLufs(momentary), tr("LUFS"), modern::textColor()},
		{tr("RANGE"), QString::number(range, 'f', 1), tr("LU"), modern::textColor()},
		{tr("TRUE PEAK"), std::isfinite(truePeak) ? QString::number(truePeak, 'f', 1) : QString::fromUtf8("–∞"),
			tr("dBTP"), overCeiling ? dangerColor() : modern::textColor()},
	}};
	const qreal gap = 6;
	const qreal tileWidth = (readoutArea.width() - bigWidth - gap * 4) / 2;
	const qreal tileHeight = (readoutArea.height() - gap) / 2;
	QFont value = font();
	value.setPixelSize(16);
	value.setBold(true);
	for (int i = 0; i < 4; ++i)
	{
		const QRectF tile(bigTile.right() + gap + (i % 2) * (tileWidth + gap),
			readoutArea.top() + (i / 2) * (tileHeight + gap), tileWidth + gap, tileHeight);
		paintTile(tile);
		const auto& r = readouts[i];
		// Place text by baseline: fonts with tall line boxes would otherwise drift to the middle
		const QRectF inner = tile.adjusted(10, 7, -10, -7);
		const QFontMetricsF captionFm(caption);
		const QFontMetricsF valueFm(value);
		p.setFont(caption);
		p.setPen(modern::dimTextColor());
		p.drawText(QPointF(inner.left(), inner.top() + captionFm.capHeight()), r.caption);
		const qreal unitWidth = captionFm.horizontalAdvance(r.unit);
		p.drawText(QPointF(inner.right() - unitWidth, inner.bottom()), r.unit);
		p.setFont(value);
		p.setPen(r.color);
		p.drawText(QPointF(inner.right() - unitWidth - 5 - valueFm.horizontalAdvance(r.value), inner.bottom()), r.value);
	}
}

} // namespace lmms::gui
