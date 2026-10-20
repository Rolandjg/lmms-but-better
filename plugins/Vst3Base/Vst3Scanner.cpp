/* Vst3Scanner.cpp - isolate third-party plugin discovery from LMMS.
 * Copyright (c) 2026 LMMS developers. Licensed under GPL-2.0-or-later.
 */
#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstring>
#ifndef Q_OS_WIN
#include <sys/resource.h>
#endif

#include "Vst3Basics.h"
#include "Vst3NativeModule.h"
#include "Vst3HostApp.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

int main(int argc, char** argv)
{
#ifdef Q_OS_WIN
	// a crashing plugin must end the scan, not wait on a Windows error dialog
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#else
	const rlimit noCore{0, 0};
	setrlimit(RLIMIT_CORE, &noCore);
#endif
	QApplication app{argc, argv};
	// arguments() is lossless for non-ASCII paths, unlike argv on Windows
	const QStringList arguments = QCoreApplication::arguments();
	if (arguments.size() != 3) { return 1; }
	using namespace Steinberg;
	using namespace lmms::vst3;
	QJsonArray classes;
	{
		Vst3NativeModule module;
		QString error;
		if (!module.open(arguments[1], &error))
		{
			qWarning().noquote() << error;
			return 2;
		}
		using Factory = IPluginFactory* (PLUGIN_API*)();
		const auto getFactory = reinterpret_cast<Factory>(module.symbol("GetPluginFactory"));
		if (!getFactory) { return 3; }
		auto factory = owned(getFactory());
		if (!factory) { return 4; }
		FUnknownPtr<IPluginFactory3> factory3{factory};
		if (factory3) { factory3->setHostContext(Vst3HostApp::instance()); }
		FUnknownPtr<IPluginFactory2> factory2{factory};
		PFactoryInfo vendor{};
		factory->getFactoryInfo(&vendor);

		for (int32 index = 0; index < factory->countClasses(); ++index)
		{
			PClassInfo info{};
			PClassInfo2 info2{};
			QString uid, name, categories, version, maker;
			if (factory2 && factory2->getClassInfo2(index, &info2) == kResultOk)
			{
				if (std::strcmp(info2.category, kVstAudioEffectClass) != 0) { continue; }
				uid = tuidToString(info2.cid);
				name = QString::fromUtf8(info2.name);
				categories = QString::fromUtf8(info2.subCategories);
				version = QString::fromUtf8(info2.version);
				maker = QString::fromUtf8(info2.vendor);
			}
			else if (factory->getClassInfo(index, &info) == kResultOk)
			{
				if (std::strcmp(info.category, kVstAudioEffectClass) != 0) { continue; }
				uid = tuidToString(info.cid);
				name = QString::fromUtf8(info.name);
			}
			else { continue; }
			if (maker.isEmpty()) { maker = QString::fromUtf8(vendor.vendor); }
			classes.append(QJsonObject{{"uid", uid}, {"name", name}, {"vendor", maker},
				{"version", version}, {"categories", categories}});
		}
		factory2 = nullptr;
		factory3 = nullptr;
		factory = nullptr;
	}
	// Write only after teardown succeeds. Plugin stdout is not our protocol.
	QFile output{arguments[2]};
	if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return 5; }
	const auto json = QJsonDocument{classes}.toJson(QJsonDocument::Compact);
	return output.write(json) == json.size() ? 0 : 6;
}
