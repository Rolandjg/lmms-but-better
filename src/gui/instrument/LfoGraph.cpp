/*
 * LfoGraph.cpp - Displays LFO graphs
 *
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 * Copyright (c) 2024-     Michael Gregorius
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

#include "LfoGraph.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include "EnvelopeAndLfoParameters.h"
#include "Oscillator.h"
#include "ColorHelper.h"
#include "ModernWidgets.h"

#include "FontHelper.h"

namespace lmms
{

extern const float SECS_PER_LFO_OSCILLATION;

namespace gui
{

LfoGraph::LfoGraph(QWidget* parent) :
	QWidget(parent),
	ModelView(nullptr, this),
	m_noAmountColor(96, 101, 110),
	m_fullAmountColor(modern::accentColor())
{
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	setCursor(Qt::SizeVerCursor);
	setToolTip(tr("Drag up or down to set the LFO amount, double-click to switch it on/off.\n"
		"Drop a sample here to use it as the LFO wave."));
}

void LfoGraph::mousePressEvent(QMouseEvent* me)
{
	auto* params = castModel<EnvelopeAndLfoParameters>();
	if (me->button() != Qt::LeftButton || !params) { return; }
	auto& amountModel = params->getLfoAmountModel();
	m_dragging = true;
	m_dragStartY = me->pos().y();
	m_dragStartAmount = amountModel.value();
	amountModel.addJournalCheckPoint();
	amountModel.saveJournallingState(false);
}

void LfoGraph::mouseMoveEvent(QMouseEvent* me)
{
	auto* params = castModel<EnvelopeAndLfoParameters>();
	if (!m_dragging || !params) { return; }
	const float fine = me->modifiers() & Qt::ShiftModifier ? 0.1f : 1.f;
	// 100 pixels span the whole amount range of -1..1, independent of the display size
	const float delta = (m_dragStartY - me->pos().y()) / 50.f * fine;
	params->getLfoAmountModel().setValue(m_dragStartAmount + delta);
}

void LfoGraph::mouseReleaseEvent(QMouseEvent*)
{
	auto* params = castModel<EnvelopeAndLfoParameters>();
	if (m_dragging && params) { params->getLfoAmountModel().restoreJournallingState(); }
	m_dragging = false;
}

void LfoGraph::mouseDoubleClickEvent(QMouseEvent* me)
{
	if (me->button() == Qt::LeftButton) { toggleAmountModel(); }
}

void LfoGraph::paintEvent(QPaintEvent*)
{
	QPainter p{this};
	p.setRenderHint(QPainter::Antialiasing);

	modern::paintDisplay(p, rect(), 8, 2);

	const auto* params = castModel<EnvelopeAndLfoParameters>();
	if (!params) { return; }

	const float amount = params->getLfoAmountModel().value();
	const float lfoSpeed = params->getLfoSpeedModel().value();
	const f_cnt_t predelayFrames = params->getLfoPredelayFrames();
	const f_cnt_t attackFrames = params->getLfoAttackFrames();
	const f_cnt_t oscillationFrames = params->getLfoOscillationFrames();
	const bool x100 = params->getX100Model().value();
	const int lfoWaveModel = params->getLfoWaveModel().value();
	const auto * userWave = params->getLfoUserWave().get();

	const int margin = 5;
	const int lfoGraphWidth = width() - 2 * margin;
	const int lfoGraphHeight = height() - 2 * margin;
	const int graphBaseX = margin;
	const int graphBaseY = margin + lfoGraphHeight / 2;

	const float framesForGraph =
		SECS_PER_LFO_OSCILLATION * Engine::audioEngine()->baseSampleRate() / 10;

	float oscFrames = oscillationFrames * (x100 ? 100. : 1.);

	QPolygonF polyLine;

	// Collect the points for the poly line by sampling the LFO according to its shape
	for (int x = 0; x <= lfoGraphWidth; ++x)
	{
		float value = 0.0;
		float currentSample = x * framesForGraph / lfoGraphWidth;
		const auto sampleAsFrameCount = static_cast<f_cnt_t>(currentSample);
		if (sampleAsFrameCount > predelayFrames)
		{
			currentSample -= predelayFrames;
			const float phase = currentSample / oscFrames;

			const auto lfoShape = static_cast<EnvelopeAndLfoParameters::LfoShape>(lfoWaveModel);
			switch (lfoShape)
			{
				case EnvelopeAndLfoParameters::LfoShape::SineWave:
				default:
					value = Oscillator::sinSample(phase);
					break;
				case EnvelopeAndLfoParameters::LfoShape::TriangleWave:
					value = Oscillator::triangleSample(phase);
					break;
				case EnvelopeAndLfoParameters::LfoShape::SawWave:
					value = Oscillator::sawSample(phase);
					break;
				case EnvelopeAndLfoParameters::LfoShape::SquareWave:
					value = Oscillator::squareSample(phase);
					break;
				case EnvelopeAndLfoParameters::LfoShape::RandomWave:
					if (x % (int)(900 * lfoSpeed + 1) == 0)
					{
						m_randomGraph = Oscillator::noiseSample(0.0);
					}
					value = m_randomGraph;
					break;
				case EnvelopeAndLfoParameters::LfoShape::UserDefinedWave:
					value = Oscillator::userWaveSample(userWave, phase);
					break;
			}

			if (sampleAsFrameCount <= attackFrames)
			{
				value *= currentSample / attackFrames;
			}
		}

		// The wave is scaled by the amount, so the display shows how far the LFO swings
		const float currentY = -lfoGraphHeight / 2.0f * value * (amount == 0.f ? 1.f : amount);

		polyLine << QPointF(graphBaseX + x, graphBaseY + currentY);
	}

	// Compute the color of the lines based on the amount of the LFO
	const float absAmount = std::abs(amount);
	const QColor lineColor{ColorHelper::interpolateInRgb(m_noAmountColor, m_fullAmountColor, absAmount)};

	QPainterPath area;
	area.moveTo(graphBaseX, graphBaseY);
	area.addPolygon(polyLine);
	area.lineTo(graphBaseX + lfoGraphWidth, graphBaseY);
	area.closeSubpath();
	QColor fill = lineColor;
	fill.setAlpha(amount == 0.f ? 14 : 34);
	p.fillPath(area, fill);

	p.setPen(QPen(lineColor, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	p.drawPolyline(polyLine);

	drawInfoText(p, *params);
}

void LfoGraph::drawInfoText(QPainter& p, const EnvelopeAndLfoParameters& params)
{
	const float lfoSpeed = params.getLfoSpeedModel().value();
	const bool x100 = params.getX100Model().value();
	const float amount = params.getLfoAmountModel().value();

	const float hertz = 1. / (SECS_PER_LFO_OSCILLATION * lfoSpeed) * (x100 ? 100. : 1.);
	const auto infoText = m_dragging
		? tr("AMT %1%").arg(qRound(amount * 100))
		: (amount == 0.f ? tr("OFF · %1 Hz") : tr("%1 Hz")).arg(hertz, 0, 'f', hertz < 10 ? 2 : 1);

	const auto f = adjustedToPixelSize(font(), SMALL_FONT_SIZE);
	p.setFont(f);
	const auto fm = QFontMetrics(f);
	const int chipWidth = fm.horizontalAdvance(infoText) + 8;
	const QRectF chip(width() - chipWidth - 3, 3, chipWidth, fm.capHeight() + 7);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(0, 0, 0, 170));
	p.drawRect(chip);
	p.setPen(m_dragging ? modern::accentColor() : modern::dimTextColor());
	p.drawText(QPointF(chip.left() + 4, chip.top() + 3.5 + fm.capHeight()), infoText);
}

void LfoGraph::toggleAmountModel()
{
	auto* params = castModel<EnvelopeAndLfoParameters>();
	auto& lfoAmountModel = params->getLfoAmountModel();

	lfoAmountModel.setValue(lfoAmountModel.value() < 1.0 ? 1.0 : 0.0);
}

} // namespace gui

} // namespace lmms
