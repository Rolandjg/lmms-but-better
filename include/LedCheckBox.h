/*
 * LedCheckBox.h - class LedCheckBox, an improved QCheckBox
 *
 * Copyright (c) 2005-2008 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef LMMS_GUI_LED_CHECKBOX_H
#define LMMS_GUI_LED_CHECKBOX_H

#include "AutomatableButton.h"


class QPainter;


namespace lmms::gui
{

class LMMS_EXPORT LedCheckBox : public AutomatableButton
{
	Q_OBJECT
public:
	enum class LedColor
	{
		Yellow,
		Green,
		Red
	} ;

	LedCheckBox( const QString & _txt, QWidget * _parent,
				const QString & _name = QString(),
						LedColor _color = LedColor::Yellow,
						bool legacyMode = true);
	LedCheckBox( QWidget * _parent,
				const QString & _name = QString(),
						LedColor _color = LedColor::Yellow,
						bool legacyMode = true);

	inline const QString & text()
	{
		return( m_text );
	}

	void setText( const QString& s );
	void setLedColor(LedColor color);

	Q_PROPERTY( QString text READ text WRITE setText )
	Q_PROPERTY(QColor yellowColor READ getYellowColor WRITE setYellowColor)
	Q_PROPERTY(QColor greenColor READ getGreenColor WRITE setGreenColor)
	Q_PROPERTY(QColor redColor READ getRedColor WRITE setRedColor)
	Q_PROPERTY(QColor offColor READ getOffColor WRITE setOffColor)

	QColor const & getYellowColor() const { return m_yellowColor; }
	void setYellowColor(QColor const & color) { m_yellowColor = color; update(); }

	QColor const & getGreenColor() const { return m_greenColor; }
	void setGreenColor(QColor const & color) { m_greenColor = color; update(); }

	QColor const & getRedColor() const { return m_redColor; }
	void setRedColor(QColor const & color) { m_redColor = color; update(); }

	QColor const & getOffColor() const { return m_offColor; }
	void setOffColor(QColor const & color) { m_offColor = color; update(); }

	//! Draws a LED into the given rect, either lit in the given color or off
	static void paintLed(QPainter& p, const QRectF& rect, const QColor& onColor, const QColor& offColor, bool on);

	//! The area covered by a LED, matching the footprint of the old led_*.png pixmaps
	static constexpr int LedWidth = 16;
	static constexpr int LedHeight = 14;

protected:
	void paintEvent( QPaintEvent * _pe ) override;


private:
	QString m_text;

	LedColor m_ledColor;

	QColor m_yellowColor {0, 206, 186};
	QColor m_greenColor {10, 220, 96};
	QColor m_redColor {255, 36, 36};
	QColor m_offColor {80, 97, 112};

	bool m_legacyMode;


	QColor const & onColor() const;

	void onTextUpdated(); //!< to be called when you updated @a m_text
	void paintLegacy(QPaintEvent * p);
	void paintNonLegacy(QPaintEvent * p);

} ;


} // namespace lmms::gui

#endif // LMMS_GUI_LED_CHECKBOX_H
