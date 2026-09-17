/*
 * Vst3Basics.cpp - basic helpers for the VST3 host
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

#include "Vst3Basics.h"

#include <cstring>

namespace lmms::vst3
{

QString tuidToString(const Steinberg::TUID tuid)
{
	QString result;
	result.reserve(32);
	for (int i = 0; i < 16; ++i)
	{
		result += QString("%1").arg(static_cast<unsigned char>(tuid[i]), 2, 16, QChar('0')).toUpper();
	}
	return result;
}




bool tuidFromString(const QString& str, Steinberg::TUID tuid)
{
	if (str.size() != 32) { return false; }
	for (int i = 0; i < 16; ++i)
	{
		bool ok = false;
		const unsigned byte = str.mid(i * 2, 2).toUInt(&ok, 16);
		if (!ok) { return false; }
		tuid[i] = static_cast<Steinberg::int8>(byte);
	}
	return true;
}




QString fromVstString(const Steinberg::Vst::TChar* str)
{
	if (!str) { return QString(); }
	return QString::fromUtf16(reinterpret_cast<const char16_t*>(str));
}




void toVstString(Steinberg::Vst::String128 dst, const QString& src)
{
	const int len = std::min(src.size(), 127);
	std::memcpy(dst, src.utf16(), len * sizeof(Steinberg::Vst::TChar));
	dst[len] = 0;
}


} // namespace lmms::vst3
