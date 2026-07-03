/*
 * Vst3Basics.h - basic helpers for the VST3 host
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

#ifndef LMMS_VST3_BASICS_H
#define LMMS_VST3_BASICS_H

#include <QString>

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

#include "vst3base_export.h"

namespace lmms::vst3
{

//! Convert a 16 byte class id into a 32 char hex string
QString VST3BASE_EXPORT tuidToString(const Steinberg::TUID tuid);

//! Parse a 32 char hex string into a 16 byte class id; returns false on error
bool VST3BASE_EXPORT tuidFromString(const QString& str, Steinberg::TUID tuid);

//! Convert a null-terminated UTF-16 VST3 string into a QString
QString VST3BASE_EXPORT fromVstString(const Steinberg::Vst::TChar* str);

//! Copy a QString into a null-terminated UTF-16 String128
void VST3BASE_EXPORT toVstString(Steinberg::Vst::String128 dst, const QString& src);

} // namespace lmms::vst3

#endif // LMMS_VST3_BASICS_H
