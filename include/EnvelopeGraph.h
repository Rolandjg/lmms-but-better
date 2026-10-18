/*
 * EnvelopeGraph.h - Displays envelope graphs
 *
 * Copyright (c) 2004-2009 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef LMMS_GUI_ENVELOPE_GRAPH_H
#define LMMS_GUI_ENVELOPE_GRAPH_H

#include <QWidget>
#include <array>

#include "ModelView.h"

namespace lmms
{

class EnvelopeAndLfoParameters;
class FloatModel;

namespace gui
{

//! Envelope display whose points can be dragged: pre-delay, attack, hold and release move
//! sideways, the decay point also moves up and down to set the sustain level. The round handle
//! in the middle of the attack, decay and release segments bends them into curves.
class EnvelopeGraph : public QWidget, public ModelView
{
	Q_OBJECT
	Q_PROPERTY(QColor noAmountColor MEMBER m_noAmountColor)
	Q_PROPERTY(QColor fullAmountColor MEMBER m_fullAmountColor)
	Q_PROPERTY(QColor markerFillColor MEMBER m_markerFillColor)
	Q_PROPERTY(QColor markerOutlineColor MEMBER m_markerOutlineColor)

public:
	enum class ScalingMode
	{
		Dynamic,
		Absolute,
		Relative
	};

public:
	EnvelopeGraph(QWidget* parent);

	QSize sizeHint() const override { return QSize(240, 64); }
	QSize minimumSizeHint() const override { return QSize(120, 48); }

protected:
	void modelChanged() override;

	void mousePressEvent(QMouseEvent*) override;
	void mouseMoveEvent(QMouseEvent*) override;
	void mouseReleaseEvent(QMouseEvent*) override;
	void mouseDoubleClickEvent(QMouseEvent*) override;
	void leaveEvent(QEvent*) override;
	void contextMenuEvent(QContextMenuEvent*) override;
	void paintEvent(QPaintEvent*) override;

private:
	//! The draggable points, in the order they appear on the curve
	enum class Handle { None = -1, Predelay, Attack, Hold, Decay, Release, AttackCurve, DecayCurve, ReleaseCurve };

	struct Geometry
	{
		QRectF plot;
		float segmentWidth; //!< pixels for a segment at its maximum value
		std::array<QPointF, 5> points;
		std::array<QPointF, 3> curveHandles; //!< middle of the attack, decay and release segments
	};

	Geometry geometry() const;
	Handle handleAt(const QPointF& pos) const;
	FloatModel* timeModel(Handle handle) const;
	FloatModel* curveModel(Handle handle) const;
	QString readout(Handle handle) const;
	void toggleAmountModel();

private:
	EnvelopeAndLfoParameters* m_params = nullptr;

	//! Fit the envelope to the width, like the envelope editors of Vital or Serum
	ScalingMode m_scaling = ScalingMode::Relative;

	Handle m_hovered = Handle::None;
	Handle m_dragged = Handle::None;
	QPointF m_dragStart;
	float m_dragStartTime = 0.f;
	float m_dragStartSustain = 0.f;
	float m_dragStartCurve = 0.f;
	float m_dragSegmentWidth = 1.f;

	QColor m_noAmountColor;
	QColor m_fullAmountColor;
	QColor m_markerFillColor;
	QColor m_markerOutlineColor;
};

} // namespace gui

} // namespace lmms

#endif // LMMS_GUI_ENVELOPE_GRAPH_H
