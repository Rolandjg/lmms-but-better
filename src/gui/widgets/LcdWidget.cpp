/*
 * LcdWidget.cpp - a widget for displaying numbers in LCD style
 *
 * Copyright (c) 2005-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 * Copyright (c) 2008 Paul Giblock <pgllama/at/gmail.com>
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



#include <algorithm>
#include <array>

#include <QPainter>
#include <QPolygonF>
#include <QStyleOptionFrame>

#include "LcdWidget.h"
#include "DeprecationHelper.h"
#include "FontHelper.h"


namespace lmms::gui
{

namespace
{

// Bits identifying the segments of a seven-segment digit:
//
//   -A-
//  F   B
//   -G-
//  E   C
//   -D-
enum Segment
{
	SegA = 1 << 0,
	SegB = 1 << 1,
	SegC = 1 << 2,
	SegD = 1 << 3,
	SegE = 1 << 4,
	SegF = 1 << 5,
	SegG = 1 << 6,
	SegsAll = SegA | SegB | SegC | SegD | SegE | SegF | SegG
};

constexpr std::array<int, 10> digitSegments = {
	SegA | SegB | SegC | SegD | SegE | SegF,        // 0
	SegB | SegC,                                    // 1
	SegA | SegB | SegG | SegE | SegD,               // 2
	SegA | SegB | SegG | SegC | SegD,               // 3
	SegF | SegG | SegB | SegC,                      // 4
	SegA | SegF | SegG | SegC | SegD,               // 5
	SegA | SegF | SegG | SegE | SegD | SegC,        // 6
	SegA | SegB | SegC,                             // 7
	SegsAll,                                        // 8
	SegA | SegB | SegC | SegD | SegF | SegG         // 9
};

//! Hexagonal outline of one segment with the classic beveled LCD ends
QPolygonF segmentPolygon(const QPointF& from, const QPointF& to, float halfThickness)
{
	const float ht = halfThickness;
	if (from.y() == to.y()) // horizontal segment
	{
		return QPolygonF({
			{from.x(), from.y()},
			{from.x() + ht, from.y() - ht},
			{to.x() - ht, to.y() - ht},
			{to.x(), to.y()},
			{to.x() - ht, to.y() + ht},
			{from.x() + ht, from.y() + ht}});
	}

	return QPolygonF({ // vertical segment
		{from.x(), from.y()},
		{from.x() + ht, from.y() + ht},
		{to.x() + ht, to.y() - ht},
		{to.x(), to.y()},
		{to.x() - ht, to.y() - ht},
		{from.x() - ht, from.y() + ht}});
}

} // namespace


//! Segment colors for a named style ("19green", "21pink", ...)
LcdWidget::SegmentPalette LcdWidget::paletteForStyle(const QString& style)
{
	// Colors match the previous lcd_*.png glyph sheet artwork
	if (style.contains("red")) { return {QColor(0, 0, 0), QColor(175, 14, 40), QColor(26, 2, 6)}; }
	if (style.contains("purple")) { return {QColor(0, 0, 0), QColor(146, 99, 202), QColor(10, 8, 25)}; }
	if (style.contains("pink")) { return {QColor(0, 0, 0), QColor(238, 2, 155), QColor(10, 8, 25)}; }

	// green is the default
	return {QColor(0, 0, 0), QColor(11, 213, 86), QColor(2, 32, 13)};
}




//! Digit cell size in pixels for a named style
QSize LcdWidget::cellSizeForStyle(const QString& style)
{
	// Cell sizes match the previous glyph sheets: small "11" styles and the
	// large default used by everything else
	return style.startsWith("11") ? QSize(6, 11) : QSize(11, 19);
}




LcdWidget::LcdWidget(QWidget* parent, const QString& name, bool leadingZero) :
	LcdWidget(1, parent, name, leadingZero)
{
}




LcdWidget::LcdWidget(int numDigits, QWidget* parent, const QString& name, bool leadingZero) :
	LcdWidget(numDigits, QString("19green"), parent, name, leadingZero)
{
}




LcdWidget::LcdWidget(int numDigits, const QString& style, QWidget* parent, const QString& name, bool leadingZero) :
	QWidget( parent ),
	m_label(),
	m_textColor( 255, 255, 255 ),
	m_textShadowColor( 64, 64, 64 ),
	m_numDigits(numDigits),
	m_seamlessLeft(false),
	m_seamlessRight(false),
	m_leadingZero(leadingZero)
{
	initUi( name, style );
}

void LcdWidget::setValue(int value)
{
	QString s = m_textForValue[value];
	if (s.isEmpty())
	{
		s = QString::number(value);
		if (m_leadingZero)
		{
			s = s.rightJustified(m_numDigits, '0');
		}
	}

	if (m_display != s)
	{
		m_display = s;

		update();
	}
}

void LcdWidget::setValue(float value)
{
	if (-1 < value && value < 0)
	{
		QString s = QString::number(static_cast<int>(value));
		s.prepend('-');

		if (m_display != s)
		{
			m_display = s;
			update();
		}
	}
	else
	{
		setValue(static_cast<int>(value));
	}
}




QColor LcdWidget::textColor() const
{
	return m_textColor;
}

void LcdWidget::setTextColor( const QColor & c )
{
	m_textColor = c;
}




QColor LcdWidget::textShadowColor() const
{
	return m_textShadowColor;
}

void LcdWidget::setTextShadowColor( const QColor & c )
{
	m_textShadowColor = c;
}




//! Draws the segments given by the bitmask lit into the given digit cell
void LcdWidget::drawSegments(QPainter& p, const QRectF& cell, int mask, const QColor& on, const QColor& off) const
{
	// All geometry scales with the cell so any style stays readable
	const QRectF r = cell.adjusted(cell.width() * 0.14f, cell.height() * 0.09f,
		-cell.width() * 0.14f, -cell.height() * 0.09f);
	const float t = std::max(1.f, static_cast<float>(r.width()) * 0.24f);
	const float ht = t * 0.5f;
	//! small gap between the segment ends
	const float gap = std::max(0.35f, t * 0.3f);

	// Centerlines of the six segment end points
	const float lx = r.left() + ht;
	const float rx = r.right() - ht;
	const float ty = r.top() + ht;
	const float my = r.center().y();
	const float by = r.bottom() - ht;

	const std::array<QPolygonF, 7> segments = {
		segmentPolygon({lx + gap, ty}, {rx - gap, ty}, ht),   // A
		segmentPolygon({rx, ty + gap}, {rx, my - gap}, ht),   // B
		segmentPolygon({rx, my + gap}, {rx, by - gap}, ht),   // C
		segmentPolygon({lx + gap, by}, {rx - gap, by}, ht),   // D
		segmentPolygon({lx, my + gap}, {lx, by - gap}, ht),   // E
		segmentPolygon({lx, ty + gap}, {lx, my - gap}, ht),   // F
		segmentPolygon({lx + gap, my}, {rx - gap, my}, ht)    // G
	};

	for (std::size_t i = 0; i < segments.size(); i++)
	{
		// unlit segments still show faintly, like on a real LCD
		p.setBrush(mask & (1 << i) ? on : off);
		p.drawPolygon(segments[i]);
	}
}




void LcdWidget::paintEvent( QPaintEvent* )
{
	QPainter p( this );

	constexpr int margin = 1;

	const int marginX1 = m_seamlessLeft ? 0 : margin + m_marginWidth;
	const int marginX2 = m_seamlessRight ? 0 : margin + m_marginWidth;
	const int lcdWidth = m_cellWidth * m_numDigits + marginX1 + marginX2;

	// Display background including the margins around the digits
	p.fillRect(QRect(m_seamlessLeft ? 0 : margin, margin,
		lcdWidth - (m_seamlessLeft ? 0 : margin) - (m_seamlessRight ? 0 : margin), m_cellHeight),
		m_palette.background);

	// Dim the segments when the widget is disabled, mirroring the grayed-out
	// row of the old glyph sheets
	const QColor on = isEnabled() ? m_palette.on : m_palette.on.darker(160);
	const QColor off = isEnabled() ? m_palette.off : m_palette.off.darker(160);

	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);

	float x = marginX1;

	// Padding: empty cells showing only ghost segments
	for (int i = 0; i < m_numDigits - m_display.length(); i++)
	{
		drawSegments(p, QRectF(x, margin, m_cellWidth, m_cellHeight), 0, on, off);
		x += m_cellWidth;
	}

	// Digits
	for (const auto& digit : m_display)
	{
		const int val = digit.digitValue();
		const int mask = val >= 0 ? digitSegments[val] : (digit == '-' ? SegG : 0);
		drawSegments(p, QRectF(x, margin, m_cellWidth, m_cellHeight), mask, on, off);
		x += m_cellWidth;
	}

	p.restore();

	// Border
	// When either the left or right edge is seamless, the border drawing must be done
	// by the encapsulating class (usually LcdFloatSpinBox).
	if (!m_seamlessLeft && !m_seamlessRight)
	{
		QStyleOptionFrame opt;
		opt.initFrom(this);
		opt.state = QStyle::State_Sunken;
		opt.rect = QRect(0, 0, m_cellWidth * m_numDigits + (margin + m_marginWidth) * 2 - 1,
			m_cellHeight + (margin * 2));

		style()->drawPrimitive(QStyle::PE_Frame, &opt, &p, this);
	}

	// Label
	if( !m_label.isEmpty() )
	{
		p.setFont(adjustedToPixelSize(p.font(), DEFAULT_FONT_SIZE));
		p.setPen( textShadowColor() );
		p.drawText(width() / 2 -
				p.fontMetrics().horizontalAdvance(m_label) / 2 + 1,
						height(), m_label);
		p.setPen( textColor() );
		p.drawText(width() / 2 -
				p.fontMetrics().horizontalAdvance(m_label) / 2,
						height() - 1, m_label);
	}

}




void LcdWidget::setLabel( const QString& label )
{
	m_label = label;
	updateSize();
}




void LcdWidget::setMarginWidth( int width )
{
	m_marginWidth = width;

	updateSize();
}




void LcdWidget::updateSize()
{
	const int marginX1 = m_seamlessLeft ? 0 : 1 + m_marginWidth;
	const int marginX2 = m_seamlessRight ? 0 : 1 + m_marginWidth;
	const int marginY = 1;
	if (m_label.isEmpty())
	{
		setFixedSize(
			m_cellWidth * m_numDigits + marginX1 + marginX2,
			m_cellHeight + (2 * marginY)
		);
	}
	else
	{
		setFixedSize(
			qMax<int>(
				m_cellWidth * m_numDigits + marginX1 + marginX2,
				QFontMetrics(adjustedToPixelSize(font(), DEFAULT_FONT_SIZE)).horizontalAdvance(m_label)
			),
			m_cellHeight + (2 * marginY) + 9
		);
	}

	update();
}




void LcdWidget::initUi(const QString& name , const QString& style)
{
	setEnabled( true );

	setWindowTitle( name );

	m_palette = paletteForStyle(style);

	const QSize cellSize = cellSizeForStyle(style);
	m_cellWidth = cellSize.width();
	m_cellHeight = cellSize.height();

	m_marginWidth =  m_cellWidth / 2;

	updateSize();
}

} // namespace lmms::gui
