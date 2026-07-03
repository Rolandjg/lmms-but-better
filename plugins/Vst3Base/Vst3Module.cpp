/*
 * Vst3Module.cpp - loading of VST3 module shared objects
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

#include "Vst3Module.h"

#include <dlfcn.h>

#include <QDir>
#include <QFileInfo>
#include <map>
#include <mutex>

namespace lmms::vst3
{

namespace
{

using ModuleEntryFunc = bool (PLUGIN_API*)(void*);
using ModuleExitFunc = bool (PLUGIN_API*)();
using GetFactoryProc = Steinberg::IPluginFactory* (PLUGIN_API*)();

std::mutex s_moduleCacheMutex;
std::map<QString, std::weak_ptr<Vst3Module>>& moduleCache()
{
	static std::map<QString, std::weak_ptr<Vst3Module>> s_cache;
	return s_cache;
}

const char* archDirName()
{
#if defined(__x86_64__)
	return "x86_64-linux";
#elif defined(__i386__)
	return "i386-linux";
#elif defined(__aarch64__)
	return "aarch64-linux";
#elif defined(__arm__)
	return "arm-linux";
#else
	return "unknown-linux";
#endif
}

} // namespace


QString Vst3Module::resolveModulePath(const QString& path)
{
	const QFileInfo info{path};
	if (!info.isDir()) { return path; }

	// bundle format: Foo.vst3/Contents/<arch>-linux/Foo.so
	const QDir contents{info.absoluteFilePath() + "/Contents"};
	QDir archDir{contents.absolutePath() + "/" + archDirName()};
	if (!archDir.exists())
	{
		// be lenient and take any architecture directory containing a .so
		for (const auto& entry : contents.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
		{
			QDir candidate{contents.absolutePath() + "/" + entry};
			if (!candidate.entryList({"*.so"}, QDir::Files).isEmpty())
			{
				archDir = candidate;
				break;
			}
		}
	}

	const QString soName = info.completeBaseName() + ".so";
	if (archDir.exists(soName)) { return archDir.absoluteFilePath(soName); }

	const auto soFiles = archDir.entryList({"*.so"}, QDir::Files);
	if (!soFiles.isEmpty()) { return archDir.absoluteFilePath(soFiles.first()); }

	return QString();
}




std::shared_ptr<Vst3Module> Vst3Module::open(const QString& path, QString* error)
{
	const QString canonical = QFileInfo{path}.canonicalFilePath();
	const QString key = canonical.isEmpty() ? path : canonical;

	std::lock_guard<std::mutex> lock{s_moduleCacheMutex};
	if (const auto it = moduleCache().find(key); it != moduleCache().end())
	{
		if (auto existing = it->second.lock()) { return existing; }
	}

	const QString soPath = resolveModulePath(key);
	if (soPath.isEmpty() || !QFileInfo::exists(soPath))
	{
		if (error) { *error = QString("no shared object found in \"%1\"").arg(path); }
		return nullptr;
	}

	void* handle = dlopen(soPath.toLocal8Bit().constData(), RTLD_LAZY | RTLD_LOCAL);
	if (!handle)
	{
		if (error) { *error = QString("dlopen failed: %1").arg(dlerror()); }
		return nullptr;
	}

	auto module = std::shared_ptr<Vst3Module>(new Vst3Module());
	module->m_handle = handle;
	module->m_path = key;

	// ModuleEntry/ModuleExit are required by the VST3 spec on Linux, but
	// tolerate modules that lack them
	if (const auto entry = reinterpret_cast<ModuleEntryFunc>(dlsym(handle, "ModuleEntry")))
	{
		if (!entry(handle))
		{
			if (error) { *error = QString("ModuleEntry failed for \"%1\"").arg(soPath); }
			return nullptr;
		}
		module->m_entered = true;
	}

	const auto getFactory = reinterpret_cast<GetFactoryProc>(dlsym(handle, "GetPluginFactory"));
	if (!getFactory)
	{
		if (error) { *error = QString("\"%1\" exports no GetPluginFactory").arg(soPath); }
		return nullptr;
	}

	module->m_factory = Steinberg::owned(getFactory());
	if (!module->m_factory)
	{
		if (error) { *error = QString("GetPluginFactory returned nothing for \"%1\"").arg(soPath); }
		return nullptr;
	}

	moduleCache()[key] = module;
	module->m_inCache = true;
	return module;
}




Vst3Module::~Vst3Module()
{
	m_factory = nullptr;
	if (m_handle)
	{
		if (m_entered)
		{
			if (const auto exit = reinterpret_cast<ModuleExitFunc>(dlsym(m_handle, "ModuleExit")))
			{
				exit();
			}
		}
		dlclose(m_handle);
	}

	// modules that failed to load fully die inside open() with the cache
	// mutex already held - they are not in the cache, so don't lock
	if (m_inCache)
	{
		std::lock_guard<std::mutex> lock{s_moduleCacheMutex};
		const auto it = moduleCache().find(m_path);
		if (it != moduleCache().end() && it->second.expired()) { moduleCache().erase(it); }
	}
}


} // namespace lmms::vst3
