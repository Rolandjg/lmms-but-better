/* Platform module loading shared by the host and isolated scanner.
 * Copyright (c) 2026 LMMS developers. Licensed under GPL-2.0-or-later.
 */
#ifndef LMMS_VST3_NATIVE_MODULE_H
#define LMMS_VST3_NATIVE_MODULE_H

#include <QFile>
#include <QFileInfo>
#include <QString>
#include <dlfcn.h>
#ifdef Q_OS_MACOS
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace lmms::vst3
{
class Vst3NativeModule
{
public:
	Vst3NativeModule() = default;
	Vst3NativeModule(const Vst3NativeModule&) = delete;
	Vst3NativeModule& operator=(const Vst3NativeModule&) = delete;
	~Vst3NativeModule()
	{
		if (m_entered)
		{
			if (auto exit = reinterpret_cast<bool (*)()>(symbol(exitName()))) { exit(); }
		}
		if (m_handle) { dlclose(m_handle); }
#ifdef Q_OS_MACOS
		if (m_bundle) { CFRelease(m_bundle); }
#endif
	}

#ifdef Q_OS_MACOS
	static CFBundleRef createBundle(const QString& path)
	{
		const auto bytes = QFile::encodeName(QFileInfo{path}.absoluteFilePath());
		auto url = CFURLCreateFromFileSystemRepresentation(nullptr,
			reinterpret_cast<const UInt8*>(bytes.constData()), bytes.size(), true);
		if (!url) { return nullptr; }
		auto bundle = CFBundleCreate(nullptr, url);
		CFRelease(url);
		return bundle;
	}

	static QString executablePath(const QString& path)
	{
		auto bundle = createBundle(path);
		if (!bundle) { return {}; }
		auto url = CFBundleCopyExecutableURL(bundle);
		CFRelease(bundle);
		if (!url) { return {}; }
		auto absolute = CFURLCopyAbsoluteURL(url);
		CFRelease(url);
		if (!absolute) { return {}; }
		auto string = CFURLCopyFileSystemPath(absolute, kCFURLPOSIXPathStyle);
		CFRelease(absolute);
		if (!string) { return {}; }
		const auto length = CFStringGetLength(string);
		QString result(length, Qt::Uninitialized);
		CFStringGetCharacters(string, CFRangeMake(0, length), reinterpret_cast<UniChar*>(result.data()));
		CFRelease(string);
		return result;
	}
#endif

	bool open(const QString& path, QString* error = nullptr)
	{
		QString binary = path;
		void* entryArgument = nullptr;
#ifdef Q_OS_MACOS
		QString bundlePath = QFileInfo{path}.absoluteFilePath();
		const int contents = bundlePath.indexOf(".vst3/Contents/");
		if (contents >= 0) { bundlePath.truncate(contents + 5); }
		m_bundle = createBundle(bundlePath);
		if (!m_bundle)
		{
			if (error) { *error = QString("Invalid VST3 bundle: %1").arg(path); }
			return false;
		}
		binary = executablePath(bundlePath);
		entryArgument = m_bundle;
#endif
		// Keep code mapped while asynchronous plugin threads unwind after exit.
		m_handle = dlopen(QFile::encodeName(binary).constData(), RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
		if (!m_handle)
		{
			if (error) { *error = QString("dlopen failed: %1").arg(dlerror()); }
			return false;
		}
#ifndef Q_OS_MACOS
		entryArgument = m_handle;
#endif
		auto entry = reinterpret_cast<bool (*)(void*)>(symbol(entryName()));
#ifdef Q_OS_MACOS
		if (!entry || !symbol(exitName()))
		{
			if (error) { *error = QString("Missing VST3 bundle entry points: %1").arg(path); }
			return false;
		}
#endif
		if (entry)
		{
			if (!entry(entryArgument))
			{
				if (error) { *error = QString("VST3 module entry failed: %1").arg(path); }
				return false;
			}
			m_entered = true;
		}
		return true;
	}

	void* symbol(const char* name) const { return dlsym(m_handle, name); }

private:
	static const char* entryName()
	{
#ifdef Q_OS_MACOS
		return "bundleEntry";
#else
		return "ModuleEntry";
#endif
	}
	static const char* exitName()
	{
#ifdef Q_OS_MACOS
		return "bundleExit";
#else
		return "ModuleExit";
#endif
	}
	void* m_handle = nullptr;
	bool m_entered = false;
#ifdef Q_OS_MACOS
	CFBundleRef m_bundle = nullptr;
#endif
};
} // namespace lmms::vst3
#endif
