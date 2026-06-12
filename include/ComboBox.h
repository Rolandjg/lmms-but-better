/*
 * ComboBox.h - class ComboBox, a combo box view for models
 *
 * Copyright (c) 2006-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef LMMS_GUI_COMBOBOX_H
#define LMMS_GUI_COMBOBOX_H

#include <QMenu>
#include <QWidget>

#include "ComboBoxModel.h"
#include "AutomatableModelView.h"

namespace lmms::gui
{

class LMMS_EXPORT ComboBox : public QWidget, public IntModelView
{
	Q_OBJECT
	Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor)
	Q_PROPERTY(QColor borderColor READ borderColor WRITE setBorderColor)
	Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor)
public:
	ComboBox( QWidget* parent = nullptr, const QString& name = QString() );
	~ComboBox() override = default;

	ComboBoxModel* model()
	{
		return castModel<ComboBoxModel>();
	}

	const ComboBoxModel* model() const
	{
		return castModel<ComboBoxModel>();
	}

	QColor const & backgroundColor() const { return m_backgroundColor; }
	void setBackgroundColor(QColor const & color) { m_backgroundColor = color; }

	QColor const & borderColor() const { return m_borderColor; }
	void setBorderColor(QColor const & color) { m_borderColor = color; }

	QColor const & textColor() const { return m_textColor; }
	void setTextColor(QColor const & color) { m_textColor = color; }

	static constexpr int DEFAULT_HEIGHT = 22;

public slots:
	void selectNext();
	void selectPrevious();


protected:
	void contextMenuEvent( QContextMenuEvent* event ) override;
	void mousePressEvent( QMouseEvent* event ) override;
	void paintEvent( QPaintEvent* event ) override;
	void wheelEvent( QWheelEvent* event ) override;


private:
	QPixmap m_arrow = embed::getIconPixmap("combobox_arrow");
	QPixmap m_arrowSelected = embed::getIconPixmap("combobox_arrow_selected");

	QColor m_backgroundColor = QColor(16, 18, 19);
	QColor m_borderColor = QColor(58, 64, 71);
	QColor m_textColor = QColor(209, 216, 228);

	QMenu m_menu;

	bool m_pressed;


private slots:
	void setItem( QAction* item );

} ;

} // namespace lmms::gui

#endif // LMMS_GUI_COMBOBOX_H
