/*
 * ControlAccessibility.h - keyboard focus and assistive-technology support for custom-drawn controls
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

#ifndef LMMS_GUI_CONTROL_ACCESSIBILITY_H
#define LMMS_GUI_CONTROL_ACCESSIBILITY_H

#include "lmms_export.h"

class QWidget;

namespace lmms::gui
{

//! Draws an outline over @p control while it has keyboard (Tab) focus. Mouse focus is not outlined, so
//! clicking a control doesn't change how it looks.
LMMS_EXPORT void attachFocusRing(QWidget* control);

//! Tells assistive technology that the value of @p control changed (no-op unless one is listening)
LMMS_EXPORT void announceValueChange(QWidget* control, float value);

//! Registers accessibility interfaces (name, role, value, range, increase/decrease actions) for the
//! custom-drawn model controls: knobs, faders, combo boxes and LCD spin boxes. Call once at startup.
LMMS_EXPORT void installControlAccessibility();

} // namespace lmms::gui

#endif // LMMS_GUI_CONTROL_ACCESSIBILITY_H
