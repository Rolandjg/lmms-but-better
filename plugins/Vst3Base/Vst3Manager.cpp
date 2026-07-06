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

#include <cstring>

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QProcessEnvironment>

#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

#include "Vst3Basics.h"
#include "Vst3Module.h"

namespace lmms::vst3
{

using namespace Steinberg;


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
	else
	{
		paths << QDir::homePath() + "/.vst3";
		paths << "/usr/lib/vst3";
		paths << "/usr/local/lib/vst3";
	}

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
	scanIfNeeded();

	for (const auto& info : m_classes)
	{
		if (info.uid == uid) { return &info; }
	}

	// the project may reference a plugin outside the search paths;
	// try the stored file location
	if (!fileHint.isEmpty() && QFileInfo::exists(fileHint))
	{
		scanModule(fileHint);
		for (const auto& info : m_classes)
		{
			if (info.uid == uid) { return &info; }
		}
	}

	return nullptr;
}




std::vector<Vst3ClassInfo> Vst3Manager::classesInFile(const QString& path)
{
	std::lock_guard<std::recursive_mutex> lock{m_mutex};
	scanModule(path);

	const QString canonical = QFileInfo{path}.canonicalFilePath();
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
			QDirIterator::Subdirectories};
		while (it.hasNext())
		{
			scanModule(it.next());
		}
	}
}




void Vst3Manager::scanModule(const QString& path)
{
	const QString canonical = QFileInfo{path}.canonicalFilePath();
	if (canonical.isEmpty()) { return; }

	for (const auto& info : m_classes)
	{
		if (info.modulePath == canonical) { return; }
	}

	QString error;
	const auto module = Vst3Module::open(canonical, &error);
	if (!module)
	{
		qWarning() << "VST3: skipping" << path << ":" << error;
		return;
	}

	IPluginFactory* factory = module->factory();
	IPluginFactory2* factory2 = nullptr;
	factory->queryInterface(IPluginFactory2::iid, reinterpret_cast<void**>(&factory2));

	PFactoryInfo factoryInfo{};
	factory->getFactoryInfo(&factoryInfo);

	const int32 count = factory->countClasses();
	for (int32 i = 0; i < count; ++i)
	{
		Vst3ClassInfo classInfo;

		if (factory2)
		{
			PClassInfo2 ci2{};
			if (factory2->getClassInfo2(i, &ci2) != kResultOk) { continue; }
			if (std::strcmp(ci2.category, kVstAudioEffectClass) != 0) { continue; }
			classInfo.uid = tuidToString(ci2.cid);
			classInfo.name = QString::fromUtf8(ci2.name);
			classInfo.vendor = QString::fromUtf8(ci2.vendor);
			classInfo.version = QString::fromUtf8(ci2.version);
			classInfo.subCategories = QString::fromUtf8(ci2.subCategories);
		}
		else
		{
			PClassInfo ci{};
			if (factory->getClassInfo(i, &ci) != kResultOk) { continue; }
			if (std::strcmp(ci.category, kVstAudioEffectClass) != 0) { continue; }
			classInfo.uid = tuidToString(ci.cid);
			classInfo.name = QString::fromUtf8(ci.name);
		}

		if (classInfo.vendor.isEmpty())
		{
			classInfo.vendor = QString::fromUtf8(factoryInfo.vendor);
		}

		classInfo.modulePath = canonical;
		if (classInfo.subCategories.isEmpty())
		{
			// no sub category information - offer the plugin in both lists
			classInfo.isInstrument = true;
			classInfo.isFx = true;
		}
		else
		{
			classInfo.isInstrument = classInfo.subCategories.contains("Instrument");
			classInfo.isFx = !classInfo.isInstrument
				|| classInfo.subCategories.contains("Fx");
		}

		m_classes.push_back(classInfo);
	}

	if (factory2) { factory2->release(); }
}


} // namespace lmms::vst3
