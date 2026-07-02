/*
 * CPULoadWidget.h - widget for displaying CPU-load (partly based on
 *                    Hydrogen's CPU-load-widget)
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

#ifndef LMMS_GUI_CPU_LOAD_WIDGET_H
#define LMMS_GUI_CPU_LOAD_WIDGET_H

#include <algorithm>
#include <QColor>
#include <QTimer>
#include <QWidget>

#include "LmmsTypes.h"


namespace lmms::gui
{


class CPULoadWidget : public QWidget
{
	Q_OBJECT
	Q_PROPERTY(int stepSize MEMBER m_stepSize)
	Q_PROPERTY(QColor textColor MEMBER m_textColor)
	Q_PROPERTY(QColor troughColor MEMBER m_troughColor)
	Q_PROPERTY(QColor loadOkColor MEMBER m_loadOkColor)
	Q_PROPERTY(QColor loadWarnColor MEMBER m_loadWarnColor)
	Q_PROPERTY(QColor loadClipColor MEMBER m_loadClipColor)
public:
	CPULoadWidget( QWidget * _parent );
	~CPULoadWidget() override = default;


protected:
	void paintEvent( QPaintEvent * _ev ) override;


protected slots:
	void updateCpuLoad();


private:
	int stepSize() const { return std::max(1, m_stepSize); }

	int m_currentLoad;

	QTimer m_updateTimer;

	int m_stepSize = 1;

	QColor m_textColor {255, 255, 255, 200};
	QColor m_troughColor {0, 0, 0, 120};
	QColor m_loadOkColor {10, 206, 170};
	QColor m_loadWarnColor {174, 240, 64};
	QColor m_loadClipColor {150, 3, 30};

} ;


} // namespace lmms::gui

#endif // LMMS_GUI_CPU_LOAD_WIDGET_H
