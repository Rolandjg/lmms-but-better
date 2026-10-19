/*
 * FontHelper.h - Header function to help with fonts
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

#ifndef LMMS_GUI_FONT_HELPER_H
#define LMMS_GUI_FONT_HELPER_H

#include <cmath>

#include <QApplication>
#include <QFont>

#include "lmms_export.h"

constexpr int DEFAULT_FONT_SIZE = 12;
constexpr int SMALL_FONT_SIZE = 10;
constexpr int LARGE_FONT_SIZE = 14;

namespace lmms::gui
{

//! Factor for the fixed-size text of custom-drawn widgets (Settings > "Text size in editors and plugins"),
//! so it can be made larger independently of display scaling. Read once at startup.
LMMS_EXPORT float customTextScale();

// Convenience method to set the font size in pixels (scaled by customTextScale())
inline QFont adjustedToPixelSize(QFont font, int size)
{
	font.setPixelSize(std::max(1, static_cast<int>(std::lround(size * customTextScale()))));
	return font;
}

} // namespace lmms::gui

#endif // LMMS_GUI_FONT_HELPER_H
