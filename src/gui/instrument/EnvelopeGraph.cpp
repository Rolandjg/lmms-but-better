/*
 * EnvelopeGraph.cpp - Displays envelope graphs
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

#include "EnvelopeGraph.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QMenu>

#include "EnvelopeAndLfoParameters.h"
#include "ColorHelper.h"
#include "FontHelper.h"
#include "ModernWidgets.h"

#include <cmath>


namespace lmms
{

extern const float SECS_PER_ENV_SEGMENT;

namespace gui
{

namespace
{

constexpr qreal HandleRadius = 3.5;
constexpr qreal GrabRadius = 7.0;

} // namespace

EnvelopeGraph::EnvelopeGraph(QWidget* parent) :
	QWidget(parent),
	ModelView(nullptr, this),
	m_noAmountColor(96, 101, 110),
	m_fullAmountColor(modern::accentColor()),
	m_markerFillColor(modern::displayColor()),
	m_markerOutlineColor(modern::textColor())
{
	setMouseTracking(true);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	setToolTip(tr("Drag the square points to shape the envelope, and the round ones up or down to bend\n"
		"the segments into curves. Shift gives fine steps. Double-click a point to reset it,\n"
		"or the background to switch the envelope on/off."));
}

void EnvelopeGraph::modelChanged()
{
	m_params = castModel<EnvelopeAndLfoParameters>();
}

EnvelopeGraph::Geometry EnvelopeGraph::geometry() const
{
	Geometry g;
	g.plot = QRectF(rect()).adjusted(6, 8, -6, -6);
	g.segmentWidth = 1.f;
	g.points.fill(g.plot.bottomLeft());
	g.curveHandles.fill(g.plot.bottomLeft());

	const auto* params = castModel<EnvelopeAndLfoParameters>();
	if (!params) { return g; }

	// For the calculation of the percentages we will for now make use of the knowledge
	// that the range goes from 0 to a positive max value, i.e. that it is in [0, max].
	const auto percentage = [](const FloatModel& model) { return model.value() / model.maxValue(); };
	const float predelayPercentage = percentage(params->getPredelayModel());
	const float attackPercentage = percentage(params->getAttackModel());
	const float holdPercentage = percentage(params->getHoldModel());
	const float decayPercentage = percentage(params->getDecayModel());
	const float releasePercentage = percentage(params->getReleaseModel());
	const float sustain = params->getSustainModel().value();

	const float availableWidth = g.plot.width();
	const float sumOfSegments = predelayPercentage + attackPercentage + holdPercentage + decayPercentage + releasePercentage;

	// The maximum width that each of the five segments (DAHDR) can occupy, according to the scaling setting
	const float absoluteWidth = availableWidth / 5.f;
	const float relativeWidth = sumOfSegments != 0.f ? availableWidth / sumOfSegments : absoluteWidth;
	// Keep the zoom while dragging, so the dragged point stays under the mouse; refit afterwards
	if (m_dragged != Handle::None) { g.segmentWidth = m_dragSegmentWidth; }
	else switch (m_scaling)
	{
		case ScalingMode::Absolute:
			g.segmentWidth = absoluteWidth;
			break;
		case ScalingMode::Relative:
			g.segmentWidth = relativeWidth;
			break;
		case ScalingMode::Dynamic:
		default:
		{
			// Uses absolute spacings but switches to relative spacing if it's running out of space
			const float preliminaryWidth = 80.f / 182.f * availableWidth;
			g.segmentWidth = sumOfSegments * preliminaryWidth > availableWidth && sumOfSegments != 0.f
				? relativeWidth
				: preliminaryWidth;
			break;
		}
	}

	const float predelayX = g.plot.left() + predelayPercentage * g.segmentWidth;
	const float attackX = predelayX + attackPercentage * g.segmentWidth;
	const float holdX = attackX + holdPercentage * g.segmentWidth;
	const float decayX = holdX + (decayPercentage * (1 - sustain)) * g.segmentWidth;
	const float releaseX = decayX + releasePercentage * g.segmentWidth;

	const qreal top = g.plot.top();
	const qreal base = g.plot.bottom();
	g.points = {
		QPointF{predelayX, base},
		QPointF{attackX, top},
		QPointF{holdX, top},
		QPointF{decayX, top + (1 - sustain) * g.plot.height()},
		QPointF{releaseX, base}
	};

	// The curve handles sit on the bent segments, halfway along them
	const auto levelToY = [&](float level) { return base - level * g.plot.height(); };
	g.curveHandles = {
		QPointF{(predelayX + attackX) / 2,
			levelToY(EnvelopeAndLfoParameters::curveValue(0.5f, 0.f, 1.f, params->getAttackCurveModel().value()))},
		QPointF{(holdX + decayX) / 2,
			levelToY(EnvelopeAndLfoParameters::curveValue(0.5f, 1.f, sustain, params->getDecayCurveModel().value()))},
		QPointF{(decayX + releaseX) / 2,
			levelToY(EnvelopeAndLfoParameters::curveValue(0.5f, sustain, 0.f, params->getReleaseCurveModel().value()))}
	};
	return g;
}

EnvelopeGraph::Handle EnvelopeGraph::handleAt(const QPointF& pos) const
{
	if (!m_params) { return Handle::None; }
	const auto g = geometry();
	// Points can sit on top of each other (e.g. attack and hold when hold is zero). The
	// earliest one wins because dragging it moves everything after it as well. Points also win
	// over the curve handles, which overlap them when a segment is very short.
	auto nearest = [&](const auto& candidates, int firstHandle)
	{
		auto best = Handle::None;
		qreal bestDistance = GrabRadius;
		for (std::size_t i = 0; i < candidates.size(); ++i)
		{
			const qreal distance = std::hypot(pos.x() - candidates[i].x(), pos.y() - candidates[i].y());
			if (distance < bestDistance - 0.5)
			{
				best = static_cast<Handle>(firstHandle + static_cast<int>(i));
				bestDistance = distance;
			}
		}
		return best;
	};
	const auto point = nearest(g.points, static_cast<int>(Handle::Predelay));
	return point != Handle::None ? point : nearest(g.curveHandles, static_cast<int>(Handle::AttackCurve));
}

FloatModel* EnvelopeGraph::timeModel(Handle handle) const
{
	if (!m_params) { return nullptr; }
	switch (handle)
	{
		case Handle::Predelay: return &m_params->getPredelayModel();
		case Handle::Attack: return &m_params->getAttackModel();
		case Handle::Hold: return &m_params->getHoldModel();
		case Handle::Decay: return &m_params->getDecayModel();
		case Handle::Release: return &m_params->getReleaseModel();
		default: break;
	}
	return nullptr;
}

FloatModel* EnvelopeGraph::curveModel(Handle handle) const
{
	if (!m_params) { return nullptr; }
	switch (handle)
	{
		case Handle::AttackCurve: return &m_params->getAttackCurveModel();
		case Handle::DecayCurve: return &m_params->getDecayCurveModel();
		case Handle::ReleaseCurve: return &m_params->getReleaseCurveModel();
		default: break;
	}
	return nullptr;
}

QString EnvelopeGraph::readout(Handle handle) const
{
	if (const FloatModel* curve = curveModel(handle))
	{
		const QString name = handle == Handle::AttackCurve ? tr("ATTACK")
			: handle == Handle::DecayCurve ? tr("DECAY") : tr("RELEASE");
		const int percent = qRound(curve->value() * 100);
		return tr("%1 CURVE %2%3%").arg(name, percent > 0 ? "+" : "").arg(percent);
	}

	const FloatModel* model = timeModel(handle);
	if (!model) { return QString(); }

	// Same time curve as EnvelopeAndLfoParameters::updateSampleVars()
	auto formatTime = [](float value)
	{
		const float seconds = SECS_PER_ENV_SEGMENT * EnvelopeAndLfoParameters::expKnobVal(value);
		return seconds < 1.f ? tr("%1 ms").arg(seconds * 1000.f, 0, 'f', 0) : tr("%1 s").arg(seconds, 0, 'f', 2);
	};

	switch (handle)
	{
		case Handle::Predelay: return tr("DELAY %1").arg(formatTime(model->value()));
		case Handle::Attack: return tr("ATTACK %1").arg(formatTime(model->value()));
		case Handle::Hold: return tr("HOLD %1").arg(formatTime(model->value()));
		case Handle::Decay:
		{
			const float sustain = m_params->getSustainModel().value();
			return tr("DECAY %1 · SUSTAIN %2%")
				.arg(formatTime(model->value() * (1 - sustain)))
				.arg(qRound(sustain * 100));
		}
		case Handle::Release: return tr("RELEASE %1").arg(formatTime(model->value()));
		default: break;
	}
	return QString();
}

void EnvelopeGraph::mousePressEvent(QMouseEvent* me)
{
	if (me->button() != Qt::LeftButton || !m_params) { return; }

	// Measured before m_dragged is set, which freezes the zoom
	const float segmentWidth = geometry().segmentWidth;
	m_dragged = handleAt(me->pos());
	FloatModel* model = timeModel(m_dragged);
	if (!model) { model = curveModel(m_dragged); }
	if (!model) { return; }

	// Shaping an envelope that is switched off would do nothing audible, so switch it on
	auto& amountModel = m_params->getAmountModel();
	if (amountModel.value() == 0.f) { amountModel.setValue(1.f); }

	m_dragStart = me->pos();
	m_dragStartCurve = model->value();
	m_dragStartTime = model->value();
	m_dragStartSustain = m_params->getSustainModel().value();
	m_dragSegmentWidth = std::max(1.f, segmentWidth);
	model->addJournalCheckPoint();
	model->saveJournallingState(false);
	if (m_dragged == Handle::Decay)
	{
		m_params->getSustainModel().addJournalCheckPoint();
		m_params->getSustainModel().saveJournallingState(false);
	}
	update();
}

void EnvelopeGraph::mouseMoveEvent(QMouseEvent* me)
{
	const float fine = me->modifiers() & Qt::ShiftModifier ? 0.1f : 1.f;
	const QPointF delta = me->pos() - m_dragStart;

	if (FloatModel* curve = curveModel(m_dragged))
	{
		// 40 pixels up bend the segment all the way upwards
		curve->setValue(m_dragStartCurve - static_cast<float>(delta.y()) / 40.f * fine);
		update();
		return;
	}

	FloatModel* model = timeModel(m_dragged);
	if (!model)
	{
		const auto hovered = handleAt(me->pos());
		if (hovered != m_hovered)
		{
			m_hovered = hovered;
			setCursor(hovered == Handle::None ? Qt::ArrowCursor
				: hovered == Handle::Decay ? Qt::SizeAllCursor
				: curveModel(hovered) ? Qt::SizeVerCursor : Qt::SizeHorCursor);
			update();
		}
		return;
	}

	const float sustain = m_params->getSustainModel().value();
	// The decay segment is drawn shortened by the sustain level, so it moves faster per pixel
	const float widthScale = m_dragged == Handle::Decay ? std::max(0.05f, 1.f - sustain) : 1.f;
	const float timePerPixel = model->maxValue() / (m_dragSegmentWidth * widthScale);
	model->setValue(m_dragStartTime + static_cast<float>(delta.x()) * timePerPixel * fine);

	if (m_dragged == Handle::Decay)
	{
		const float height = std::max<qreal>(1., geometry().plot.height());
		m_params->getSustainModel().setValue(m_dragStartSustain - static_cast<float>(delta.y()) / height * fine);
	}
	update();
}

void EnvelopeGraph::mouseReleaseEvent(QMouseEvent*)
{
	if (FloatModel* curve = curveModel(m_dragged)) { curve->restoreJournallingState(); }
	if (FloatModel* model = timeModel(m_dragged))
	{
		model->restoreJournallingState();
		if (m_dragged == Handle::Decay) { m_params->getSustainModel().restoreJournallingState(); }
	}
	m_dragged = Handle::None;
	update();
}

void EnvelopeGraph::mouseDoubleClickEvent(QMouseEvent* me)
{
	if (me->button() != Qt::LeftButton || !m_params) { return; }
	const auto handle = handleAt(me->pos());
	if (FloatModel* curve = curveModel(handle)) { curve->reset(); }
	else if (FloatModel* model = timeModel(handle))
	{
		model->reset();
		if (handle == Handle::Decay) { m_params->getSustainModel().reset(); }
	}
	else { toggleAmountModel(); }
}

void EnvelopeGraph::leaveEvent(QEvent*)
{
	if (m_dragged == Handle::None && m_hovered != Handle::None)
	{
		m_hovered = Handle::None;
		update();
	}
}

void EnvelopeGraph::contextMenuEvent(QContextMenuEvent* event)
{
	QMenu menu(this);
	QMenu* scalingMenu = menu.addMenu(tr("Scaling"));
	scalingMenu->setToolTipsVisible(true);

	auto switchTo = [&](ScalingMode scaling)
	{
		if (m_scaling != scaling)
		{
			m_scaling = scaling;
			update();
		}
	};

	auto addScalingEntry = [scalingMenu, &switchTo, this](const QString& text, const QString& toolTip, ScalingMode scaling)
	{
		QAction* action = scalingMenu->addAction(text, [&switchTo, scaling]() { switchTo(scaling); });
		action->setCheckable(true);
		action->setChecked(m_scaling == scaling);
		action->setToolTip(toolTip);
	};
	
	addScalingEntry(
		tr("Dynamic"),
		tr("Uses absolute spacings but switches to relative spacing if it's running out of space"),
		ScalingMode::Dynamic);
	addScalingEntry(
		tr("Absolute"),
		tr("Provides enough potential space for each segment but does not scale"),
		ScalingMode::Absolute);
	addScalingEntry(
		tr("Relative"),
		tr("Always uses all of the available space to display the envelope graph"),
		ScalingMode::Relative);

    menu.exec(event->globalPos());
}

void EnvelopeGraph::paintEvent(QPaintEvent*)
{
	QPainter p{this};
	p.setRenderHint(QPainter::Antialiasing);

	modern::paintDisplay(p, rect(), 8, 4);

	const auto* params = castModel<EnvelopeAndLfoParameters>();
	if (!params) { return; }

	const auto g = geometry();
	const auto& pts = g.points;

	// The curve fades from grey to the accent color as the amount grows
	const float amount = params->getAmountModel().value();
	const QColor lineColor{ColorHelper::interpolateInRgb(m_noAmountColor, m_fullAmountColor, std::abs(amount))};

	// Sample the bent segments: attack (pre-delay -> attack point), decay (hold -> decay point)
	// and release (decay point -> release point)
	const float sustain = params->getSustainModel().value();
	QPainterPath curve;
	curve.moveTo(g.plot.left(), g.plot.bottom());
	auto addSegment = [&](const QPointF& from, const QPointF& to, float fromLevel, float toLevel, float bend)
	{
		const int steps = std::clamp(static_cast<int>(to.x() - from.x()), 1, 48);
		for (int i = 1; i <= steps; ++i)
		{
			const float t = static_cast<float>(i) / steps;
			const float level = EnvelopeAndLfoParameters::curveValue(t, fromLevel, toLevel, bend);
			curve.lineTo(from.x() + (to.x() - from.x()) * t, g.plot.bottom() - level * g.plot.height());
		}
	};
	curve.lineTo(pts[0]);
	addSegment(pts[0], pts[1], 0.f, 1.f, params->getAttackCurveModel().value());
	curve.lineTo(pts[2]);
	addSegment(pts[2], pts[3], 1.f, sustain, params->getDecayCurveModel().value());
	addSegment(pts[3], pts[4], sustain, 0.f, params->getReleaseCurveModel().value());

	QPainterPath area = curve;
	area.lineTo(pts[4].x(), g.plot.bottom());
	area.closeSubpath();
	QColor fill = lineColor;
	fill.setAlpha(amount == 0.f ? 18 : 40);
	p.fillPath(area, fill);

	// Mark where the note is released: everything right of the sustain point is the release
	QColor releaseLine = modern::dimTextColor();
	releaseLine.setAlpha(110);
	p.setPen(QPen(releaseLine, 1, Qt::DashLine));
	p.drawLine(QPointF(pts[3].x(), g.plot.top() - 4), QPointF(pts[3].x(), g.plot.bottom() + 3));

	p.setPen(QPen(lineColor, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	p.setBrush(Qt::NoBrush);
	p.drawPath(curve);

	const auto active = m_dragged != Handle::None ? m_dragged : m_hovered;

	// Round curve handles, smaller than the points; hidden where a segment is too short to bend
	for (std::size_t i = 0; i < g.curveHandles.size(); ++i)
	{
		const auto handle = static_cast<Handle>(static_cast<int>(Handle::AttackCurve) + static_cast<int>(i));
		const QPointF segmentStart = i == 0 ? pts[0] : i == 1 ? pts[2] : pts[3];
		const QPointF segmentEnd = i == 0 ? pts[1] : i == 1 ? pts[3] : pts[4];
		if (std::hypot(segmentEnd.x() - segmentStart.x(), segmentEnd.y() - segmentStart.y()) < 4 * HandleRadius
			&& active != handle)
		{
			continue;
		}
		const bool highlighted = active == handle;
		const qreal r = highlighted ? HandleRadius : HandleRadius - 1;
		p.setPen(QPen(highlighted ? m_fullAmountColor : m_markerOutlineColor, 1.2));
		p.setBrush(highlighted ? m_fullAmountColor : m_markerFillColor);
		p.drawEllipse(g.curveHandles[i], r, r);
	}

	// Square handles: hollow until hovered or dragged
	for (std::size_t i = 0; i < pts.size(); ++i)
	{
		const bool highlighted = static_cast<int>(active) == static_cast<int>(i);
		const qreal r = highlighted ? HandleRadius + 1 : HandleRadius;
		p.setPen(QPen(highlighted ? m_fullAmountColor : m_markerOutlineColor, 1.2));
		p.setBrush(highlighted ? m_fullAmountColor : m_markerFillColor);
		p.drawRect(QRectF(pts[i].x() - r, pts[i].y() - r, 2 * r, 2 * r));
	}

	// Readout chip in the top right corner: the dragged/hovered value, otherwise the state
	QString text = readout(active);
	if (text.isEmpty())
	{
		text = amount == 0.f ? tr("OFF") : tr("AMT %1%").arg(qRound(amount * 100));
	}
	auto f = adjustedToPixelSize(font(), SMALL_FONT_SIZE);
	p.setFont(f);
	const auto fm = QFontMetrics(f);
	const int chipWidth = fm.horizontalAdvance(text) + 8;
	const QRectF chip(width() - chipWidth - 3, 3, chipWidth, fm.capHeight() + 7);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(0, 0, 0, 170));
	p.drawRect(chip);
	p.setPen(active != Handle::None ? modern::accentColor() : modern::dimTextColor());
	p.drawText(QPointF(chip.left() + 4, chip.top() + 3.5 + fm.capHeight()), text);
}

void EnvelopeGraph::toggleAmountModel()
{
	auto* params = castModel<EnvelopeAndLfoParameters>();
	auto& amountModel = params->getAmountModel();

	amountModel.setValue(amountModel.value() < 1.0 ? 1.0 : 0.0);
}

} // namespace gui

} // namespace lmms
