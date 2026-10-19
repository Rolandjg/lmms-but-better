/*
 * GroupBox.cpp - groupbox for LMMS
 *
 * Copyright (c) 2005-2009 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include <QMouseEvent>
#include <QPainter>

#include "GroupBox.h"
#include "DeprecationHelper.h"
#include "embed.h"
#include "FontHelper.h"
#include "ModernWidgets.h"

namespace lmms::gui
{


GroupBox::GroupBox( const QString & _caption, QWidget * _parent ) :
	QWidget( _parent ),
	BoolModelView( nullptr, this ),
	m_caption( _caption ),
	m_titleBarHeight( 11 )
{
	// Power-symbol toggle, drawn to match the flat section headers (ModernHeader)
	auto ledPixmap = [](bool on)
	{
		QPixmap pixmap(11, 11);
		pixmap.fill(Qt::transparent);
		QPainter p(&pixmap);
		p.setRenderHint(QPainter::Antialiasing);
		QPen pen(on ? modern::accentColor() : modern::dimTextColor(), 1.4);
		pen.setCapStyle(Qt::RoundCap);
		p.setPen(pen);
		p.drawArc(QRectF(1.5, 2, 8, 8), 120 * 16, 300 * 16);
		p.drawLine(QPointF(5.5, 0.8), QPointF(5.5, 5.2));
		return pixmap;
	};
	m_led = new PixmapButton( this, _caption );
	m_led->setCheckable( true );
	m_led->move( 3, 1 );
	m_led->setActiveGraphic(ledPixmap(true));
	m_led->setInactiveGraphic(ledPixmap(false));
	m_led->setCursor(Qt::PointingHandCursor);
	m_led->setToolTip(tr("Turn %1 on/off").arg(_caption.toLower()));

	setModel( new BoolModel( false, nullptr, _caption, true ) );
	setAutoFillBackground( true );
	unsetCursor();
}




GroupBox::~GroupBox()
{
	delete m_led;
}




void GroupBox::modelChanged()
{
	m_led->setModel( model() );
}


bool GroupBox::ledButtonShown() const
{
	return !m_led->isHidden();
}


void GroupBox::setLedButtonShown(bool value)
{
	m_led->setVisible(value);
}


void GroupBox::mousePressEvent( QMouseEvent * _me )
{
	const auto pos = position(_me);

	if (ledButtonShown() && pos.y() > 1 && pos.y() < 13 && _me->button() == Qt::LeftButton)
	{
		model()->setValue(!model()->value());
	}
}




void GroupBox::paintEvent( QPaintEvent * pe )
{
	QPainter p( this );

	// Draw background
	p.fillRect( 0, 0, width(), height(), p.background() );

	// Flat header: small bold caps followed by a hairline, no box
	auto f = adjustedToPixelSize(font(), SMALL_FONT_SIZE);
	f.setBold(true);
	f.setLetterSpacing(QFont::AbsoluteSpacing, 0.6);
	p.setFont(f);
	const auto fm = QFontMetrics(f);
	const int baseline = (m_titleBarHeight + 2 + fm.capHeight()) / 2 + 1;

	const bool on = !ledButtonShown() || (model() && model()->value());
	p.setPen(on ? modern::textColor() : modern::dimTextColor());
	int const captionX = ledButtonShown() ? 18 : 4;
	p.drawText(captionX, baseline, m_caption);

	const int lineX = captionX + fm.horizontalAdvance(m_caption) + 6;
	if (lineX < width() - 4)
	{
		p.setPen(QColor(255, 255, 255, 18));
		const int y = baseline - fm.capHeight() / 2;
		p.drawLine(lineX, y, width() - 4, y);
	}
}


} // namespace lmms::gui
