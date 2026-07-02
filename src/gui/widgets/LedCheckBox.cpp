/*
 * LedCheckBox.cpp - class LedCheckBox, an improved QCheckBox
 *
 * Copyright (c) 2005-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "LedCheckBox.h"

#include <algorithm>
#include <QFontMetrics>
#include <QPainter>

#include "DeprecationHelper.h"
#include "FontHelper.h"

namespace lmms::gui
{


LedCheckBox::LedCheckBox( const QString & _text, QWidget * _parent,
				const QString & _name, LedColor _color, bool legacyMode ) :
	AutomatableButton( _parent, _name ),
	m_text( _text ),
	m_ledColor( _color ),
	m_legacyMode(legacyMode)
{
	setLedColor(_color);
}




LedCheckBox::LedCheckBox( QWidget * _parent,
				const QString & _name, LedColor _color, bool legacyMode ) :
	LedCheckBox( QString(), _parent, _name, _color, legacyMode )
{
}

void LedCheckBox::setText( const QString &s )
{
	m_text = s;
	onTextUpdated();
}




void LedCheckBox::paintEvent( QPaintEvent * pe )
{
	if (!m_legacyMode)
	{
		paintNonLegacy(pe);
	}
	else
	{
		paintLegacy(pe);
	}
}




void LedCheckBox::setLedColor(LedColor color)
{
	setCheckable( true );

	m_ledColor = color;

	if (m_legacyMode){ setFont(adjustedToPixelSize(font(), DEFAULT_FONT_SIZE)); }

	setText( m_text );
}




QColor const & LedCheckBox::onColor() const
{
	switch (m_ledColor)
	{
		case LedColor::Green: return m_greenColor;
		case LedColor::Red: return m_redColor;
		case LedColor::Yellow:
		default: return m_yellowColor;
	}
}




//! Draws a LED into the given rect, either lit in the given color or off
void LedCheckBox::paintLed(QPainter& p, const QRectF& rect, const QColor& onColor, const QColor& offColor, bool on)
{
	const QPointF center = rect.center();
	// The core of the LED and the glow around it scale with the given rect so
	// the LED stays crisp and proportionate at any size.
	const float coreRadius = std::min(rect.width(), rect.height()) * 0.28f;
	const float glowRadius = std::min(rect.width(), rect.height()) * 0.5f;

	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);

	if (on)
	{
		// Halo fading out around the lit core
		QColor halo = onColor;
		QRadialGradient glow(center, glowRadius);
		halo.setAlpha(110);
		glow.setColorAt(coreRadius / glowRadius, halo);
		halo.setAlpha(0);
		glow.setColorAt(1, halo);
		p.setBrush(glow);
		p.drawEllipse(center, glowRadius, glowRadius);

		// Lit core with a white-hot center
		QRadialGradient core(center, coreRadius);
		core.setColorAt(0, QColor(255, 255, 255));
		core.setColorAt(0.4, onColor.lighter(140));
		core.setColorAt(1, onColor);
		p.setBrush(core);
		p.drawEllipse(center, coreRadius, coreRadius);
	}
	else
	{
		// Faint dark rim so the LED reads as a socket even when unlit
		QColor rim = offColor.darker(220);
		rim.setAlpha(90);
		p.setBrush(rim);
		p.drawEllipse(center, coreRadius + 1.f, coreRadius + 1.f);

		p.setBrush(offColor);
		p.drawEllipse(center, coreRadius, coreRadius);
	}

	p.restore();
}




void LedCheckBox::onTextUpdated()
{
	QFontMetrics const fm = fontMetrics();

	int const width = LedWidth + 5 + fm.horizontalAdvance(text());
	int const height = m_legacyMode ? LedHeight : qMax(LedHeight, fm.height());

	setFixedSize(width, height);
}

void LedCheckBox::paintLegacy(QPaintEvent * pe)
{
	QPainter p( this );
	p.setFont(adjustedToPixelSize(font(), DEFAULT_FONT_SIZE));

	paintLed(p, QRectF(0, 0, LedWidth, LedHeight), onColor(), m_offColor, model()->value());

	p.setPen( QColor( 64, 64, 64 ) );
	p.drawText(LedWidth + 4, 11, text());
	p.setPen( QColor( 255, 255, 255 ) );
	p.drawText(LedWidth + 3, 10, text());
}

void LedCheckBox::paintNonLegacy(QPaintEvent * pe)
{
	QPainter p(this);

	paintLed(p, QRectF(0, rect().height() / 2.f - LedHeight / 2.f, LedWidth, LedHeight),
		onColor(), m_offColor, model()->value());

	QRect r = rect();
	r -= QMargins(LedWidth + 5, 0, 0, 0);
	p.drawText(r, text());
}


} // namespace lmms::gui
