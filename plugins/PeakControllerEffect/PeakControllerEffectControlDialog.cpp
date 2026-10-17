/*
 * PeakControllerEffectControlDialog.cpp - control dialog for
 *                                             PeakControllerEffect
 *
 * Copyright (c) 2008 Paul Giblock <drfaygo/at/gmail/dot/com>
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

#include "PeakControllerEffectControlDialog.h"

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
#include "ModernWidgets.h"
#include "PeakControllerEffect.h"
#include "PeakControllerEffectControls.h"
#include "embed.h"

namespace lmms::gui
{

namespace
{

QString formatDb(float linear)
{
	if (linear <= 1e-5f) { return QStringLiteral("-∞ dB"); }
	return QString("%1 dB").arg(20.f * std::log10(linear), 0, 'f', 1);
}

QString formatMs(float ms)
{
	return ms < 10.f ? QString("%1 ms").arg(ms, 0, 'f', 1) : QString("%1 ms").arg(qRound(ms));
}

} // namespace


PeakControllerEffectControlDialog::PeakControllerEffectControlDialog(
				PeakControllerEffectControls * _controls ) :
	EffectControlDialog( _controls )
{
	setWindowIcon( embed::getIconPixmap( "controller" ) );
	modern::applyWindowStyle(this);

	auto root = new QVBoxLayout(this);
	root->setContentsMargins(8, 8, 8, 8);
	root->setSpacing(6);
	root->setSizeConstraint(QLayout::SetFixedSize);

	root->addWidget(new PeakControllerDisplay(_controls, this));

	auto row = new QHBoxLayout();
	row->setSpacing(6);
	root->addLayout(row);

	auto mapping = new ModernSection(tr("Mapping"), this);
	mapping->grid()->addWidget(modern::makeKnob(this, &_controls->m_baseModel, tr("BASE"),
		tr("Base (value at silence):"), ""), 0, 0);
	mapping->grid()->addWidget(modern::makeKnob(this, &_controls->m_amountModel, tr("AMOUNT"),
		tr("Modulation amount:"), ""), 0, 1);
	mapping->grid()->addWidget(modern::makeKnob(this, &_controls->m_amountMultModel, tr("MULT"),
		tr("Amount multiplicator:"), "x"), 0, 2);
	row->addWidget(mapping);

	auto response = new ModernSection(tr("Response"), this);
	response->grid()->addWidget(modern::makeKnob(this, &_controls->m_attackModel, tr("ATTACK"),
		tr("Attack:"), ""), 0, 0);
	response->grid()->addWidget(modern::makeKnob(this, &_controls->m_decayModel, tr("RELEASE"),
		tr("Release:"), ""), 0, 1);
	row->addWidget(response);

	auto input = new ModernSection(tr("Input"), this);
	input->grid()->addWidget(modern::makeKnob(this, &_controls->m_tresholdModel, tr("THRESH"),
		tr("Threshold:"), ""), 0, 0, 2, 1);
	auto absToggle = new ModernToggle(tr("ABS"), this);
	absToggle->setModel(&_controls->m_absModel);
	absToggle->setToolTip(tr("Measure the absolute level. When off, negative signal pulls the controller down."));
	input->grid()->addWidget(absToggle, 0, 1);
	auto muteToggle = new ModernToggle(tr("MUTE"), this);
	muteToggle->setModel(&_controls->m_muteModel);
	muteToggle->setToolTip(tr("Mute this channel's output after measuring it (use it as a pure sidechain)"));
	input->grid()->addWidget(muteToggle, 1, 1);
	row->addWidget(input);
}




PeakControllerDisplay::PeakControllerDisplay(PeakControllerEffectControls* controls, QWidget* parent) :
	QWidget(parent),
	m_controls(controls)
{
	setMinimumSize(sizeHint());
	setMouseTracking(true);
	auto timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, qOverload<>(&QWidget::update));
	timer->start(33);
}




QRectF PeakControllerDisplay::plotRect() const
{
	return QRectF(rect()).adjusted(8, 8, -52, -20);
}

double PeakControllerDisplay::toY(double value) const
{
	const QRectF plot = plotRect();
	return plot.bottom() - plot.height() * std::clamp(value, 0.0, 1.0);
}

bool PeakControllerDisplay::nearThreshold(const QPointF& pos) const
{
	return plotRect().adjusted(0, -4, 0, 4).contains(pos)
		&& std::abs(pos.y() - toY(m_controls->m_tresholdModel.value())) < 6.0;
}




void PeakControllerDisplay::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	const QRectF area = QRectF(rect());
	const QRectF plot = plotRect();
	modern::paintDisplay(p, area, 0, 0);
	p.setRenderHint(QPainter::Antialiasing);
	p.setFont(adjustedToPixelSize(font(), SMALL_FONT_SIZE));
	const QFontMetricsF fm(p.font());

	p.setPen(QPen(modern::gridColor(), 1));
	for (double v : {0.25, 0.5, 0.75}) { p.drawLine(QPointF(plot.left(), toY(v)), QPointF(plot.right(), toY(v))); }

	const auto* effect = m_controls->m_effect;
	constexpr auto Size = PeakControllerEffect::HistorySize;
	const auto newest = effect->m_historyPos.load(std::memory_order_relaxed);
	auto at = [&](const auto& history, std::size_t i) { return history[(newest + i) % Size]; };
	auto toX = [&](std::size_t i) { return plot.left() + plot.width() * i / (Size - 1); };

	// Gated zone under the threshold
	const float threshold = m_controls->m_tresholdModel.value();
	QColor warn = modern::warningColor();
	if (threshold > 0.f)
	{
		warn.setAlpha(16);
		p.fillRect(QRectF(QPointF(plot.left(), toY(threshold)), plot.bottomRight()), warn);
	}

	// Measured input level
	QPainterPath inputLine;
	QPainterPath inputFill(QPointF(plot.left(), plot.bottom()));
	for (std::size_t i = 0; i < Size; ++i)
	{
		const QPointF pt(toX(i), toY(at(effect->m_inputHistory, i)));
		if (i == 0) { inputLine.moveTo(pt); } else { inputLine.lineTo(pt); }
		inputFill.lineTo(pt);
	}
	inputFill.lineTo(plot.right(), plot.bottom());
	inputFill.closeSubpath();
	QColor in = modern::secondaryColor();
	in.setAlpha(45);
	p.setPen(Qt::NoPen);
	p.setBrush(in);
	p.drawPath(inputFill);
	in.setAlpha(150);
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(in, 1));
	p.drawPath(inputLine);

	// Base level and threshold
	const float base = m_controls->m_baseModel.value();
	p.setPen(QPen(QColor(255, 255, 255, 60), 1, Qt::DotLine));
	p.drawLine(QPointF(plot.left(), toY(base)), QPointF(plot.right(), toY(base)));
	warn.setAlpha(m_dragging ? 255 : 190);
	p.setPen(QPen(warn, m_dragging ? 1.5 : 1, Qt::DashLine));
	p.drawLine(QPointF(plot.left(), toY(threshold)), QPointF(plot.right(), toY(threshold)));

	// Controller value
	QPainterPath output;
	for (std::size_t i = 0; i < Size; ++i)
	{
		const QPointF pt(toX(i), toY(at(effect->m_outputHistory, i)));
		if (i == 0) { output.moveTo(pt); } else { output.lineTo(pt); }
	}
	p.setPen(QPen(modern::accentColor(), 2));
	p.drawPath(output);

	// Labels on the lines
	p.setPen(warn);
	const QString thresholdText = tr("threshold %1").arg(formatDb(threshold));
	const double thresholdY = toY(threshold);
	p.drawText(QPointF(plot.left() + 4, thresholdY > plot.top() + 14 ? thresholdY - 3 : thresholdY + 3 + fm.capHeight()),
		thresholdText);
	p.setPen(modern::dimTextColor());
	const QString baseText = tr("base");
	const double baseY = toY(base);
	p.drawText(QPointF(plot.right() - 4 - fm.horizontalAdvance(baseText), baseY > plot.top() + 14 ? baseY - 3 : baseY + 3 + fm.capHeight()),
		baseText);

	// Current controller value as a bar on the right
	const float current = at(effect->m_outputHistory, Size - 1);
	const QRectF bar(plot.right() + 14, plot.top(), 14, plot.height());
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(255, 255, 255, 12));
	p.drawRoundedRect(bar, 2, 2);
	p.setBrush(modern::accentColor());
	p.drawRoundedRect(QRectF(bar.left(), toY(current), bar.width(), bar.bottom() - toY(current)), 2, 2);
	p.setPen(modern::textColor());
	const QString percent = QString("%1%").arg(qRound(current * 100.f));
	p.drawText(QPointF(bar.center().x() - fm.horizontalAdvance(percent) / 2, area.bottom() - 5), percent);

	// Knob values in real units
	p.setPen(modern::dimTextColor());
	const QString timing = tr("attack %1  ·  release %2")
		.arg(formatMs(PeakControllerEffect::smoothingTimeMs(m_controls->m_attackModel.value())))
		.arg(formatMs(PeakControllerEffect::smoothingTimeMs(m_controls->m_decayModel.value())));
	p.drawText(QPointF(area.left() + 8, area.bottom() - 5), timing);
	const QString level = tr("in %1").arg(formatDb(at(effect->m_inputHistory, Size - 1)));
	p.setPen(modern::secondaryColor());
	p.drawText(QPointF(plot.right() - fm.horizontalAdvance(level), area.bottom() - 5), level);
}




void PeakControllerDisplay::mousePressEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton && nearThreshold(positionF(event))) { m_dragging = true; }
}

void PeakControllerDisplay::mouseMoveEvent(QMouseEvent* event)
{
	const QPointF pos = positionF(event);
	if (m_dragging)
	{
		const QRectF plot = plotRect();
		m_controls->m_tresholdModel.setValue(static_cast<float>(std::clamp((plot.bottom() - pos.y()) / plot.height(), 0.0, 1.0)));
		return;
	}
	setCursor(nearThreshold(pos) ? Qt::SizeVerCursor : Qt::ArrowCursor);
}

void PeakControllerDisplay::mouseReleaseEvent(QMouseEvent*)
{
	m_dragging = false;
}


} // namespace lmms::gui
