/*
 * Vst3Manager.cpp - discovery of installed VST3 plugins
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

#include "Vst3Manager.h"

#include <dlfcn.h>

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QProcess>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryFile>

#include "ConfigManager.h"
#include "Vst3Module.h"

namespace lmms::vst3
{

Vst3Manager* Vst3Manager::instance()
{
	static Vst3Manager s_instance;
	return &s_instance;
}




QStringList Vst3Manager::searchPaths()
{
	QStringList paths;

	const QString envPath = QProcessEnvironment::systemEnvironment().value("VST3_PATH");
	if (!envPath.isEmpty())
	{
		for (const auto& p : envPath.split(':', Qt::SkipEmptyParts)) { paths << p; }
	}

	paths << QDir::homePath() + "/.vst3";
	paths << "/usr/lib/vst3" << "/usr/local/lib/vst3";
	const auto config = ConfigManager::inst();
	paths << config->vstDir() << config->userLadspaDir();
	paths << config->ladspaDir().split(':', Qt::SkipEmptyParts);
	paths.removeAll(QString());
	paths.removeDuplicates();

	return paths;
}




const std::vector<Vst3ClassInfo>& Vst3Manager::classes()
{
	std::lock_guard<std::recursive_mutex> lock{m_mutex};
	scanIfNeeded();
	return m_classes;
}




const Vst3ClassInfo* Vst3Manager::findByUid(const QString& uid, const QString& fileHint)
{
	std::lock_guard<std::recursive_mutex> lock{m_mutex};
	if (uid.isEmpty()) { return nullptr; }
	// Prefer the project/browser's exact module: native and bridged builds
	// may share a class UID. Also avoids scanning unrelated plugins when
	// loading a known module directly.
	if (!fileHint.isEmpty() && QFileInfo::exists(fileHint))
	{
		const QString canonical = QFileInfo{Vst3Module::hostPath(fileHint)}.canonicalFilePath();
		scanModule(canonical);
		for (const auto& info : m_classes)
		{
			if (info.uid == uid && info.modulePath == canonical) { return &info; }
		}
	}

	scanIfNeeded();
	for (const auto& info : m_classes)
	{
		if (info.uid == uid) { return &info; }
	}

	return nullptr;
}




std::vector<Vst3ClassInfo> Vst3Manager::classesInFile(const QString& path)
{
	std::lock_guard<std::recursive_mutex> lock{m_mutex};
	scanModule(path);

	const QString canonical = QFileInfo{Vst3Module::hostPath(path)}.canonicalFilePath();
	std::vector<Vst3ClassInfo> result;
	for (const auto& info : m_classes)
	{
		if (info.modulePath == canonical) { result.push_back(info); }
	}
	return result;
}




void Vst3Manager::rescan()
{
	std::lock_guard<std::recursive_mutex> lock{m_mutex};
	m_classes.clear();
	m_attemptedModules.clear();
	m_scanned = false;
	scanIfNeeded();
}




void Vst3Manager::scanIfNeeded()
{
	if (m_scanned) { return; }
	m_scanned = true;

	for (const auto& root : searchPaths())
	{
		if (!QFileInfo::exists(root)) { continue; }

		QDirIterator it{root, {"*.vst3"}, QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
			QDirIterator::Subdirectories | QDirIterator::FollowSymlinks};
		while (it.hasNext())
		{
			scanModule(it.next());
		}
	}
}




void Vst3Manager::scanModule(const QString& path)
{
	const QString canonical = QFileInfo{Vst3Module::hostPath(path)}.canonicalFilePath();
	if (canonical.isEmpty()) { return; }

	if (m_attemptedModules.contains(canonical)) { return; }
	m_attemptedModules.insert(canonical);

	// The scanner lives alongside this library, in both the build tree and
	// installed packages. Do not fall back to loading untrusted modules in
	// the UI process when the scanner is missing.
	Dl_info library{};
	if (!dladdr(reinterpret_cast<void*>(&Vst3Manager::instance), &library)) { return; }
	const QString scanner = QFileInfo{QString::fromLocal8Bit(library.dli_fname)}
		.absolutePath() + "/Vst3Scanner";
	const QString binary = Vst3Module::resolveModulePath(canonical);
	QTemporaryFile result;
	if (binary.isEmpty() || !result.open()) { return; }
	QProcess process;
	process.setProcessChannelMode(QProcess::MergedChannels);
	// Discovery happens speculatively over every installed bundle. A broken
	// plugin must not display a desktop notification just because LMMS probed
	// it; the scanner's captured output still contains the complete error.
	// Yabridge uses the session bus to report host startup failures.
	auto environment = QProcessEnvironment::systemEnvironment();
	environment.insert("DBUS_SESSION_BUS_ADDRESS", "unix:path=/dev/null");
	process.setProcessEnvironment(environment);
	process.start(scanner, {binary, result.fileName()});
	if (!process.waitForStarted(5000) || !process.waitForFinished(30000))
	{
		process.kill();
		process.waitForFinished(1000);
		qWarning() << "VST3: scanner could not finish for" << path << process.errorString();
		return;
	}
	if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
	{
		qWarning() << "VST3: skipping failed plugin" << path << process.readAll().right(2048);
		return;
	}
	const auto json = QJsonDocument::fromJson(result.readAll());
	if (!json.isArray()) { return; }
	for (const auto& value : json.array())
	{
		const auto object = value.toObject();
		Vst3ClassInfo info;
		info.uid = object.value("uid").toString();
		info.name = object.value("name").toString();
		info.vendor = object.value("vendor").toString();
		info.version = object.value("version").toString();
		info.subCategories = object.value("categories").toString();
		info.modulePath = canonical;
		const auto categories = info.subCategories.split('|');
		info.isInstrument = categories.contains("Instrument") || info.subCategories.isEmpty();
		info.isFx = categories.contains("Fx") || !info.isInstrument || info.subCategories.isEmpty();
		m_classes.push_back(info);
	}
}


} // namespace lmms::vst3
