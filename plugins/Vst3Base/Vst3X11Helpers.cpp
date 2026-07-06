/*
 * Vst3X11Helpers.cpp - X11 helpers for embedding VST3 editors
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

#include "Vst3X11Helpers.h"

#include <cstdlib>

// xcb instead of Xlib: no global error handler to fight over, errors on
// vanished child windows are simply ignored per request
#include <xcb/xcb.h>

namespace lmms::gui
{

namespace
{

xcb_connection_t* connection()
{
	static xcb_connection_t* conn = xcb_connect(nullptr, nullptr);
	return conn && !xcb_connection_has_error(conn) ? conn : nullptr;
}

} // namespace


void vst3NotifyChildWindowsOfPosition(std::uint32_t window)
{
	const auto conn = connection();
	if (!conn) { return; }

	const auto treeCookie = xcb_query_tree(conn, window);
	const auto tree = xcb_query_tree_reply(conn, treeCookie, nullptr);
	if (!tree) { return; }

	const auto translateCookie = xcb_translate_coordinates(conn, window, tree->root, 0, 0);
	const auto translated = xcb_translate_coordinates_reply(conn, translateCookie, nullptr);
	if (!translated)
	{
		std::free(tree);
		return;
	}

	const xcb_window_t* children = xcb_query_tree_children(tree);
	const int count = xcb_query_tree_children_length(tree);
	for (int i = 0; i < count; ++i)
	{
		const auto geometryCookie = xcb_get_geometry(conn, children[i]);
		const auto geometry = xcb_get_geometry_reply(conn, geometryCookie, nullptr);
		if (!geometry) { continue; }

		xcb_configure_notify_event_t event = {};
		event.response_type = XCB_CONFIGURE_NOTIFY;
		event.event = children[i];
		event.window = children[i];
		event.x = static_cast<int16_t>(translated->dst_x + geometry->x);
		event.y = static_cast<int16_t>(translated->dst_y + geometry->y);
		event.width = geometry->width;
		event.height = geometry->height;
		event.border_width = 0;
		event.above_sibling = XCB_NONE;
		event.override_redirect = 0;
		xcb_send_event(conn, 0, children[i], XCB_EVENT_MASK_STRUCTURE_NOTIFY,
			reinterpret_cast<const char*>(&event));

		std::free(geometry);
	}

	std::free(translated);
	std::free(tree);
	xcb_flush(conn);
}


} // namespace lmms::gui
