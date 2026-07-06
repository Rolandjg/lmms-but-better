/*
 * Vst3X11Helpers.h - X11 helpers for embedding VST3 editors
 *
 * Copyright (c) 2026 LMMS developers
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

#ifndef LMMS_VST3_X11_HELPERS_H
#define LMMS_VST3_X11_HELPERS_H

#include <cstdint>

namespace lmms::gui
{

//! Send a synthetic ConfigureNotify event (with root coordinates, as the
//! XEmbed convention requires) to every direct X11 child of @p window.
//! Embedded plugin GUIs - Wine/yabridge in particular, but also toolkits
//! that place popups or track drags in screen coordinates - cache their
//! screen position and only update it from these events; without them,
//! mouse coordinates go stale as soon as the window is placed by the WM
//! or moved by the user.
void vst3NotifyChildWindowsOfPosition(std::uint32_t window);

} // namespace lmms::gui

#endif // LMMS_VST3_X11_HELPERS_H
