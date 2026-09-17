/*
 * vst3_base.cpp - plugin descriptor of the VST3 base library
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

#include "LmmsCommonMacros.h"
#include "Plugin.h"
#include "vst3base_export.h"

namespace lmms
{


extern "C"
{

Plugin::Descriptor VST3BASE_EXPORT vst3base_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"VST3 Base",
	"library for all LMMS plugins hosting VST3 plugins",
	"LMMS developers",
	0x0100,
	Plugin::Type::Library,
	nullptr,
	nullptr,
	nullptr
};

}


} // namespace lmms
