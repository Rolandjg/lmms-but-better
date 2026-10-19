/*
 * FontHelper.cpp - font helpers for custom-drawn widgets
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

#include "FontHelper.h"

#include <algorithm>

#include "ConfigManager.h"

namespace lmms::gui
{

float customTextScale()
{
	// Changing it at runtime would leave already laid out widgets with the old size, so it needs a restart
	static const float scale = [] {
		const auto percent = ConfigManager::inst()->value("ui", "textscale", "100").toInt();
		return std::clamp(percent, 50, 300) / 100.f;
	}();
	return scale;
}

} // namespace lmms::gui
