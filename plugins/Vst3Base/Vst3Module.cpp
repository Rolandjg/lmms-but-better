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
#include "Vst3NativeModule.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <map>
#include <mutex>

#include "Vst3Manager.h"
#include "Vst3HostApp.h"

namespace lmms::vst3
{

namespace
{

using GetFactoryProc = Steinberg::IPluginFactory* (PLUGIN_API*)();

std::mutex s_moduleCacheMutex;
std::map<QString, std::weak_ptr<Vst3Module>>& moduleCache()
{
	static std::map<QString, std::weak_ptr<Vst3Module>> s_cache;
	return s_cache;
}

#if defined(Q_OS_WIN)
//! Bundle subdirectories this process can load, in order of preference
QStringList archDirNames()
{
#if defined(_M_X64) || defined(__x86_64__)
	return {"x86_64-win"};
#elif defined(_M_IX86) || defined(__i386__)
	return {"x86-win"};
#elif defined(_M_ARM64) || defined(__aarch64__)
	// arm64x binaries are hybrids that also load into native ARM64 hosts
	return {"arm64-win", "arm64x-win"};
#else
	return {};
#endif
}

const char* binarySuffix() { return ".vst3"; }
#elif !defined(Q_OS_MACOS)
QStringList archDirNames()
{
#if defined(__x86_64__)
	return {"x86_64-linux"};
#elif defined(__i386__)
	return {"i386-linux"};
#elif defined(__aarch64__)
	return {"aarch64-linux"};
#elif defined(__arm__)
	return {"arm-linux"};
#else
	return {"unknown-linux"};
#endif
}

const char* binarySuffix() { return ".so"; }
#endif

} // namespace


QString Vst3Module::resolveModulePath(const QString& path)
{
	const QFileInfo info{path};
	// on Windows this includes legacy single-file .vst3 DLLs
	if (!info.isDir()) { return path; }

#ifdef Q_OS_MACOS
	return Vst3NativeModule::executablePath(path);
#else
	// bundle format: Foo.vst3/Contents/<arch>-linux/Foo.so
	// or, on Windows: Foo.vst3/Contents/<arch>-win/Foo.vst3
	const QDir contents{info.absoluteFilePath() + "/Contents"};
	const QString suffix = binarySuffix();
	for (const auto& arch : archDirNames())
	{
		const QDir archDir{contents.absolutePath() + "/" + arch};
		if (!archDir.exists()) { continue; }

		const QString binaryName = info.completeBaseName() + suffix;
		if (archDir.exists(binaryName)) { return archDir.absoluteFilePath(binaryName); }

		const auto binaries = archDir.entryList({"*" + suffix}, QDir::Files);
		if (!binaries.isEmpty()) { return archDir.absoluteFilePath(binaries.first()); }
	}

	return QString();
#endif
}




QString Vst3Module::hostPath(const QString& path)
{
	QString bundle = QFileInfo{path}.absoluteFilePath();
#ifdef Q_OS_WIN
	const int contents = bundle.indexOf(".vst3/Contents/", 0, Qt::CaseInsensitive);
#else
	const int contents = bundle.indexOf(".vst3/Contents/");
#endif
	if (contents >= 0) { bundle.truncate(contents + 5); }

#ifdef Q_OS_LINUX
	const QString binary = resolveModulePath(bundle);
	QFile file{binary};
	if (!binary.isEmpty() && file.open(QIODevice::ReadOnly) && file.read(4) == QByteArray("\x7f" "ELF", 4))
	{
		return bundle;
	}

	// Yabridge keeps symlinks to the original PE binary in Contents/*-win.
	// Match their canonical targets, never just the name (different prefixes
	// can contain different plugins with identical filenames).
	const QFileInfo original{path};
	const QString canonical = original.canonicalFilePath();
	if (canonical.isEmpty()) { return bundle; }
	for (const auto& root : Vst3Manager::searchPaths())
	{
		QDirIterator it{root, {"*.vst3"}, QDir::Files | QDir::NoDotAndDotDot,
			QDirIterator::Subdirectories | QDirIterator::FollowSymlinks};
		while (it.hasNext())
		{
			const QFileInfo candidate{it.next()};
			if (!candidate.isSymLink()) { continue; }
			const QString target = candidate.canonicalFilePath();
			if (target != canonical && !(original.isDir() && target.startsWith(canonical + '/')))
			{
				continue;
			}
			QString bridged = candidate.absoluteFilePath();
			const int pos = bridged.indexOf(".vst3/Contents/");
			if (pos < 0) { continue; }
			bridged.truncate(pos + 5);
			if (!resolveModulePath(bridged).isEmpty()) { return bridged; }
		}
	}

#endif // Q_OS_LINUX
	return bundle;
}



std::shared_ptr<Vst3Module> Vst3Module::open(const QString& path, QString* error)
{
	const QString canonical = QFileInfo{hostPath(path)}.canonicalFilePath();
	const QString key = canonical.isEmpty() ? path : canonical;

	std::lock_guard<std::mutex> lock{s_moduleCacheMutex};
	if (const auto it = moduleCache().find(key); it != moduleCache().end())
	{
		if (auto existing = it->second.lock()) { return existing; }
	}

	const QString soPath = resolveModulePath(key);
	if (soPath.isEmpty() || !QFileInfo::exists(soPath))
	{
		if (error) { *error = QString("No native VST3 module found in %1. Install a plugin built for this operating system and architecture.").arg(path); }
		return nullptr;
	}

	auto module = std::shared_ptr<Vst3Module>(new Vst3Module());
	module->m_native = std::make_unique<Vst3NativeModule>();
	module->m_path = key;
	if (!module->m_native->open(soPath, error)) { return nullptr; }

	const auto getFactory = reinterpret_cast<GetFactoryProc>(module->m_native->symbol("GetPluginFactory"));
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

	Steinberg::FUnknownPtr<Steinberg::IPluginFactory3> factory3{module->m_factory};
	if (factory3) { factory3->setHostContext(Vst3HostApp::instance()); }

	moduleCache()[key] = module;
	module->m_inCache = true;
	return module;
}




Vst3Module::Vst3Module() = default;




Vst3Module::~Vst3Module()
{
	m_factory = nullptr;
	m_native.reset();

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
