/*
 * Vst3Module.h - loading of VST3 module shared objects
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

#ifndef LMMS_VST3_MODULE_H
#define LMMS_VST3_MODULE_H

#include <QString>
#include <memory>

#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/smartpointer.h"

#include "vst3base_export.h"

namespace lmms::vst3
{

//! A loaded VST3 module (shared object). Modules are shared between all
//! plugin instances loaded from the same bundle and unloaded when the last
//! user goes away.
class VST3BASE_EXPORT Vst3Module
{
public:
	~Vst3Module();
	Vst3Module(const Vst3Module&) = delete;
	Vst3Module& operator=(const Vst3Module&) = delete;

	//! Load (or reuse) the module at @p path. @p path may be a `.vst3`
	//! bundle directory, a `.vst3` single file or a plain shared object.
	//! Returns nullptr on error and sets @p error if given.
	static std::shared_ptr<Vst3Module> open(const QString& path, QString* error = nullptr);

	Steinberg::IPluginFactory* factory() const { return m_factory.get(); }

	//! The path the module was opened with (bundle path, not the inner .so)
	const QString& path() const { return m_path; }

	//! Resolve a bundle directory to the architecture specific shared
	//! object inside it; returns the input unchanged for regular files
	static QString resolveModulePath(const QString& path);

private:
	Vst3Module() = default;

	void* m_handle = nullptr;
	bool m_entered = false;
	bool m_inCache = false;
	QString m_path;
	Steinberg::IPtr<Steinberg::IPluginFactory> m_factory;
};

} // namespace lmms::vst3

#endif // LMMS_VST3_MODULE_H
