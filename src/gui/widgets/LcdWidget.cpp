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

#include "LcdWidget.h"
#include "DeprecationHelper.h"
#include "embed.h"
#include "FontHelper.h"


namespace lmms::gui
{

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




namespace
{

// Segment bits: a = top, b = top right, c = bottom right, d = bottom, e = bottom left, f = top left, g = middle
constexpr unsigned char SegA = 1, SegB = 2, SegC = 4, SegD = 8, SegE = 16, SegF = 32, SegG = 64;

unsigned char segmentsFor(QChar c)
{
	switch (c.digitValue())
	{
		case 0: return SegA | SegB | SegC | SegD | SegE | SegF;
		case 1: return SegB | SegC;
		case 2: return SegA | SegB | SegG | SegE | SegD;
		case 3: return SegA | SegB | SegG | SegC | SegD;
		case 4: return SegF | SegG | SegB | SegC;
		case 5: return SegA | SegF | SegG | SegC | SegD;
		case 6: return SegA | SegF | SegG | SegE | SegD | SegC;
		case 7: return SegA | SegB | SegC;
		case 8: return SegA | SegB | SegC | SegD | SegE | SegF | SegG;
		case 9: return SegA | SegB | SegC | SegD | SegF | SegG;
		default: return c == '-' ? SegG : 0;
	}
}

//! Six-sided segment between two points, pointed at both ends like on a real LCD
QPolygonF segment(QPointF from, QPointF to, qreal thickness)
{
	const qreal h = thickness / 2;
	if (from.y() == to.y())
	{
		return QPolygonF({from, {from.x() + h, from.y() - h}, {to.x() - h, to.y() - h},
			to, {to.x() - h, to.y() + h}, {from.x() + h, from.y() + h}});
	}
	return QPolygonF({from, {from.x() + h, from.y() + h}, {to.x() + h, to.y() - h},
		to, {to.x() - h, to.y() - h}, {from.x() - h, from.y() + h}});
}

void paintDigit(QPainter& p, const QRectF& cell, unsigned char lit, const QColor& on, const QColor& off)
{
	// Leave room between neighbouring digits and lean the digit slightly forward
	const qreal padX = std::max(1.0, cell.width() * 0.16);
	const qreal padY = std::max(1.0, cell.height() * 0.1);
	const QRectF r = cell.adjusted(padX, padY, -padX, -padY);
	const qreal t = std::clamp(r.width() * 0.24, 1.2, 3.0);
	const qreal gap = t * 0.3;
	const qreal x0 = r.left() + t / 2, x1 = r.right() - t / 2;
	const qreal y0 = r.top() + t / 2, y1 = r.center().y(), y2 = r.bottom() - t / 2;

	const std::array<QPolygonF, 7> segments = {
		segment({x0 + gap, y0}, {x1 - gap, y0}, t),
		segment({x1, y0 + gap}, {x1, y1 - gap}, t),
		segment({x1, y1 + gap}, {x1, y2 - gap}, t),
		segment({x0 + gap, y2}, {x1 - gap, y2}, t),
		segment({x0, y1 + gap}, {x0, y2 - gap}, t),
		segment({x0, y0 + gap}, {x0, y1 - gap}, t),
		segment({x0 + gap, y1}, {x1 - gap, y1}, t),
	};

	p.save();
	QTransform lean;
	lean.translate(cell.center().x(), cell.center().y());
	lean.shear(-0.07, 0);
	lean.translate(-cell.center().x(), -cell.center().y());
	p.setTransform(lean, true);
	p.setPen(Qt::NoPen);
	for (std::size_t i = 0; i < segments.size(); ++i)
	{
		p.setBrush(lit & (1 << i) ? on : off);
		p.drawPolygon(segments[i]);
	}
	p.restore();
}

} // namespace


void LcdWidget::paintDisplayBackground(QPainter& p, const QRectF& rect, bool edgeLeft, bool edgeRight)
{
	p.save();
	p.setRenderHint(QPainter::Antialiasing, false);
	// No border on seamless sides: extend past the edge and clip
	const QRectF body = rect.adjusted(edgeLeft ? 0 : -1, 0, edgeRight ? 0 : 1, 0);
	p.setClipRect(rect);
	p.fillRect(body, QColor(10, 12, 14));
	p.setPen(QColor(0, 0, 0));
	p.setBrush(Qt::NoBrush);
	p.drawRect(body.adjusted(0, 0, -1, -1));
	p.restore();
}


void LcdWidget::paintEvent( QPaintEvent* )
{
	QPainter p( this );

	const int margin = 1;
	const int left = m_seamlessLeft ? 0 : margin + m_marginWidth;
	const int right = m_seamlessRight ? 0 : margin + m_marginWidth;
	const QRectF display(0, 0, left + m_cellWidth * m_numDigits + right, m_cellHeight + 2 * margin);

	paintDisplayBackground(p, display, !m_seamlessLeft, !m_seamlessRight);

	p.setRenderHint(QPainter::Antialiasing);
	const QColor on = segmentColor();
	QColor off = on;
	off.setAlphaF(0.08);

	QRectF cell(left, margin, m_cellWidth, m_cellHeight);
	for (int i = 0; i < m_numDigits - m_display.length(); ++i)
	{
		paintDigit(p, cell, 0, on, off);
		cell.translate(m_cellWidth, 0);
	}
	for (const auto& digit : m_display)
	{
		paintDigit(p, cell, segmentsFor(digit), on, off);
		cell.translate(m_cellWidth, 0);
	}

	p.resetTransform();

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

	// We should make a factory for these or something.
	//m_lcdPixmap = embed::getIconPixmap(QString("lcd_" + style).toUtf8().constData());
	//m_lcdPixmap = embed::getIconPixmap("lcd_19green"); // TODO!!

	m_lcdPixmap = embed::getIconPixmap(QString("lcd_" + style).toUtf8().constData());
	m_cellWidth = m_lcdPixmap.size().width() / LcdWidget::charsPerPixmap;
	m_cellHeight = m_lcdPixmap.size().height() / 2;

	// The segments are drawn as vectors; the theme pixmap only supplies the colors. Its digit "8"
	// is fully lit, so its brightest pixel is the lit color (upper row enabled, lower row disabled).
	const QImage strip = m_lcdPixmap.toImage();
	const auto brightest = [&](int top) {
		QColor best(Qt::black);
		for (int y = top; y < top + m_cellHeight; ++y)
		{
			for (int x = 8 * m_cellWidth; x < 9 * m_cellWidth; ++x)
			{
				const QColor c = strip.pixelColor(x, y);
				if (c.value() > best.value()) { best = c; }
			}
		}
		return best;
	};
	m_litColor = brightest(0);
	m_disabledLitColor = brightest(m_cellHeight);

	m_marginWidth =  m_cellWidth / 2;

	updateSize();
}

} // namespace lmms::gui
