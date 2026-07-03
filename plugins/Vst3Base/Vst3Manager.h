/*
 * Vst3Manager.h - discovery of installed VST3 plugins
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

#ifndef LMMS_VST3_MANAGER_H
#define LMMS_VST3_MANAGER_H

#include <QString>
#include <QStringList>
#include <mutex>
#include <vector>

#include "pluginterfaces/base/funknown.h"

#include "vst3base_export.h"

namespace lmms::vst3
{

//! Meta data of one audio effect class found in a VST3 module
struct VST3BASE_EXPORT Vst3ClassInfo
{
	QString uid;           //!< class id as 32 char hex string
	QString name;
	QString vendor;
	QString version;
	QString subCategories; //!< e.g. "Instrument|Synth" or "Fx|Dynamics"
	QString modulePath;    //!< path of the containing bundle/file
	bool isInstrument = false;
	bool isFx = false;
};


//! Scans the standard VST3 directories and keeps a registry of all
//! discovered audio effect classes
class VST3BASE_EXPORT Vst3Manager
{
public:
	static Vst3Manager* instance();

	//! Directories that are searched for .vst3 bundles
	static QStringList searchPaths();

	//! All discovered classes; scans on first call
	const std::vector<Vst3ClassInfo>& classes();

	//! Find a class by its uid; optionally checks @p fileHint first when the
	//! uid is not in the registry (e.g. plugin dir changed). May return nullptr.
	const Vst3ClassInfo* findByUid(const QString& uid, const QString& fileHint = QString());

	void rescan();

private:
	Vst3Manager() = default;
	void scanIfNeeded();
	void scanModule(const QString& path);

	std::recursive_mutex m_mutex;
	bool m_scanned = false;
	std::vector<Vst3ClassInfo> m_classes;
};

} // namespace lmms::vst3

#endif // LMMS_VST3_MANAGER_H
