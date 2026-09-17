/* Vst3Scanner.cpp - isolate third-party plugin discovery from LMMS.
 * Copyright (c) 2026 LMMS developers. Licensed under GPL-2.0-or-later.
 */
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstring>
#include <dlfcn.h>
#include <sys/resource.h>

#include "Vst3Basics.h"
#include "Vst3HostApp.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

int main(int argc, char** argv)
{
	const rlimit noCore{0, 0};
	setrlimit(RLIMIT_CORE, &noCore);
	QApplication app{argc, argv};
	if (argc != 3) { return 1; }
	using namespace Steinberg;
	using namespace lmms::vst3;
	void* module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
	if (!module) { return 2; }
	using Entry = bool (*)(void*);
	using Exit = bool (*)();
	using Factory = IPluginFactory* (*)();
	const auto entry = reinterpret_cast<Entry>(dlsym(module, "ModuleEntry"));
	const auto exit = reinterpret_cast<Exit>(dlsym(module, "ModuleExit"));
	const auto getFactory = reinterpret_cast<Factory>(dlsym(module, "GetPluginFactory"));
	if (!getFactory || (entry && !entry(module))) { return 3; }
	auto factory = owned(getFactory());
	if (!factory) { return 4; }
	FUnknownPtr<IPluginFactory3> factory3{factory};
	if (factory3) { factory3->setHostContext(Vst3HostApp::instance()); }
	FUnknownPtr<IPluginFactory2> factory2{factory};
	PFactoryInfo vendor{};
	factory->getFactoryInfo(&vendor);
	QJsonArray classes;
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
	if (entry && exit) { exit(); }
	dlclose(module);
	// Write only after teardown succeeds. Plugin stdout is not our protocol.
	QFile output{QString::fromLocal8Bit(argv[2])};
	if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return 5; }
	const auto json = QJsonDocument{classes}.toJson(QJsonDocument::Compact);
	return output.write(json) == json.size() ? 0 : 6;
}
