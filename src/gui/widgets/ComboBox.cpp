/*
 * ComboBox.cpp - implementation of LMMS combobox
 *
 * Copyright (c) 2006-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 * Copyright (c) 2008-2009 Paul Giblock <pgib/at/users.sourceforge.net>
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


#include "ComboBox.h"

#include <QMouseEvent>
#include <QPainter>
#include <QScreen>

#include "CaptionMenu.h"
#include "FontHelper.h"
#include "DeprecationHelper.h"

namespace lmms::gui
{
const int CB_ARROW_BTN_WIDTH = 18;


ComboBox::ComboBox( QWidget * _parent, const QString & _name ) :
	QWidget( _parent ),
	IntModelView( new ComboBoxModel( nullptr, QString(), true ), this ),
	m_menu( this ),
	m_pressed( false )
{
	setFixedHeight( ComboBox::DEFAULT_HEIGHT );

	setFont(adjustedToPixelSize(font(), DEFAULT_FONT_SIZE));

	connect( &m_menu, SIGNAL(triggered(QAction*)),
				this, SLOT(setItem(QAction*)));

	setWindowTitle( _name );
	doConnections();
}






void ComboBox::selectNext()
{
	model()->setValue(model()->value() + 1);
}




void ComboBox::selectPrevious()
{
	model()->setValue(model()->value() - 1);
}



void ComboBox::contextMenuEvent( QContextMenuEvent * event )
{
	if( model() == nullptr || event->x() <= width() - CB_ARROW_BTN_WIDTH )
	{
		QWidget::contextMenuEvent( event );
		return;
	}

	CaptionMenu contextMenu( model()->displayName() );
	addDefaultActions( &contextMenu );
	contextMenu.exec( QCursor::pos() );
}




void ComboBox::mousePressEvent( QMouseEvent* event )
{
	if( model() == nullptr )
	{
		return;
	}

	const auto pos = position(event);

	if( event->button() == Qt::LeftButton && ! ( event->modifiers() & Qt::ControlModifier ) )
	{
		if (pos.x() > width() - CB_ARROW_BTN_WIDTH)
		{
			m_pressed = true;
			update();

			m_menu.clear();
			for( int i = 0; i < model()->size(); ++i )
			{
				QAction * a = m_menu.addAction( model()->itemPixmap( i ) ? model()->itemPixmap( i )->pixmap() : QPixmap(),
													model()->itemText( i ) );
				a->setData( i );
			}

			QPoint gpos = mapToGlobal(QPoint(0, height()));

			bool const menuCanBeFullyShown = screen()->geometry().contains(QRect(gpos, m_menu.sizeHint()));
			if (menuCanBeFullyShown)
			{
				m_menu.exec(gpos);
			}
			else
			{
				m_menu.exec(mapToGlobal(QPoint(width(), 0)));
			}

			m_pressed = false;
			update();
		}
		else if( event->button() == Qt::LeftButton )
		{
			selectNext();
			update();
		}
	}
	else if( event->button() == Qt::RightButton )
	{
		selectPrevious();
		update();
	}
	else
	{
		IntModelView::mousePressEvent( event );
	}
}




void ComboBox::paintEvent( QPaintEvent * _pe )
{
	QPainter p( this );
	p.setRenderHint(QPainter::Antialiasing);

	// flat rounded body with a subtle hairline border
	const QRectF body = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
	p.setPen(QPen(m_borderColor, 1));
	p.setBrush(m_pressed ? m_backgroundColor.lighter(140) : m_backgroundColor);
	p.drawRoundedRect(body, 4, 4);

	// separator in front of the arrow button
	QColor separator = m_borderColor;
	separator.setAlpha(160);
	p.setPen(separator);
	p.drawLine(QPointF(width() - CB_ARROW_BTN_WIDTH - 0.5, 4), QPointF(width() - CB_ARROW_BTN_WIDTH - 0.5, height() - 4));

	p.setRenderHint(QPainter::Antialiasing, false);

	auto arrow = m_pressed ? m_arrowSelected : m_arrow;
	p.drawPixmap(width() - CB_ARROW_BTN_WIDTH + 3, (height() - arrow.height()) / 2 + 1, arrow);

	if( model() && model()->size() > 0 )
	{
		p.setFont( font() );
		p.setClipRect( QRect( 4, 2, width() - CB_ARROW_BTN_WIDTH - 8, height() - 2 ) );
		QPixmap pm = model()->currentData() ?  model()->currentData()->pixmap() : QPixmap();
		int tx = 6;
		if( !pm.isNull() )
		{
			if( pm.height() > 16 )
			{
				pm = pm.scaledToHeight( 16, Qt::SmoothTransformation );
			}
			p.drawPixmap( tx, ( height() - pm.height() ) / 2, pm );
			tx += pm.width() + 4;
		}
		const int y = ( height()+p.fontMetrics().height() ) /2;
		p.setPen(m_textColor);
		p.drawText( tx, y-4, model()->currentText() );
	}
}




void ComboBox::wheelEvent( QWheelEvent* event )
{
	if( model() )
	{
		const int direction = (event->angleDelta().y() < 0 ? 1 : -1) * (event->inverted() ? -1 : 1);
		model()->setValue(model()->value() + direction);
		update();
		event->accept();
	}
}




void ComboBox::setItem( QAction* item )
{
	if( model() )
	{
		model()->setValue(item->data().toInt());
	}
}


} // namespace lmms::gui


