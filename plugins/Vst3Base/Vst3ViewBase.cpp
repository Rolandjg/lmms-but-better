/*
 * Vst3ViewBase.cpp - GUI classes for the VST3 host
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

#include "Vst3ViewBase.h"

#include <cmath>
#include <cstring>
#include <optional>
#include <limits>
#include <stdexcept>
#include <utility>
#include <QDebug>

#include <QCloseEvent>
#include <QEvent>
#include <QWindow>
#include <QGridLayout>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSocketNotifier>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"

#include "Knob.h"

#include "Vst3Plugin.h"
#ifdef Q_OS_LINUX
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <chrono>
#include <condition_variable>
#include <signal.h>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>

#include "ConfigManager.h"
#include "Vst3Manager.h"
#include "Vst3NativeModule.h"
#include "Vst3X11Helpers.h"
#endif
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif // Q_OS_WIN

namespace lmms::gui
{

using namespace Steinberg;

namespace
{

#if defined(Q_OS_MACOS)
const auto EditorPlatform = kPlatformTypeNSView;
const auto EditorQpa = QStringLiteral("cocoa");
#elif defined(Q_OS_WIN)
const auto EditorPlatform = kPlatformTypeHWND;
const auto EditorQpa = QStringLiteral("windows");
#else
const auto EditorPlatform = kPlatformTypeX11EmbedWindowID;
const auto EditorQpa = QStringLiteral("xcb");
#endif

bool iidEqual(const TUID a, const TUID b)
{
	return std::memcmp(a, b, sizeof(TUID)) == 0;
}

//! Content scale to report to plugins via IPlugViewContentScaleSupport.
//! Reporting the Qt device pixel ratio confuses plugins that detect the
//! system scale themselves (JUCE, DPF) or that are bridged through Wine
//! (yabridge) - the mismatch shows up as misplaced mouse hit testing. So
//! by default no scale is reported and plugins decide on their own;
//! LMMS_VST3_SCALE=1.5 forces a factor, LMMS_VST3_SCALE=auto reports the
//! device pixel ratio. On Windows the VST3 specification has the host
//! report the monitor scale, so there "auto" is the default and
//! LMMS_VST3_SCALE=none leaves it to the plugin.
#ifndef Q_OS_MACOS
std::optional<qreal> contentScaleOverride(const QWidget* w)
{
	QByteArray env = qgetenv("LMMS_VST3_SCALE");
#ifdef Q_OS_WIN
	if (env.isEmpty()) { env = "auto"; }
#endif
	if (env.isEmpty()) { return std::nullopt; }
	if (env == "auto") { return w->devicePixelRatioF(); }
	bool ok = false;
	const double value = env.toDouble(&ok);
	if (ok && value >= 0.25 && value <= 8.) { return value; }
	return std::nullopt;
}

void reportContentScale(IPlugView* view, const QWidget* w)
{
	const auto scale = contentScaleOverride(w);
	if (!view || !scale) { return; }
	IPlugViewContentScaleSupport* raw = nullptr;
	if (view->queryInterface(IPlugViewContentScaleSupport::iid,
		reinterpret_cast<void**>(&raw)) == kResultOk && raw)
	{
		auto support = owned(raw);
		support->setContentScaleFactor(static_cast<IPlugViewContentScaleSupport::ScaleFactor>(*scale));
	}
}

#endif // Q_OS_MACOS


#ifdef Q_OS_LINUX
//! A yabridge plugin whose Wine host hangs (e.g. Wine reported a stack overflow) freezes LMMS for good
//! as soon as LMMS makes an editor call: VST3 views may only be used from the GUI thread, the call
//! waits for the Wine host, and nothing interrupts it. Ctrl+C doesn't help either, since LMMS handles
//! SIGINT on the GUI thread. Once an editor call has been running for too long, this kills the
//! plugin's Wine host. yabridge then aborts LMMS, because the exception it throws on its own thread
//! can't be caught. That goes through the crash save, so the project is written to the recovery
//! file, and the hung host doesn't outlive LMMS.
//! LMMS_VST3_HANG_TIMEOUT sets the timeout in seconds, 0 disables the watchdog. Plugins that LMMS
//! loads natively are left alone: they may run a modal loop inside a callback.
class EditorHangWatchdog
{
public:
	//! Arms the watchdog for one editor call; nested calls run under the outermost one's deadline
	class Scope
	{
	public:
		Scope(const char* operation, const vst3::Vst3Plugin* plugin)
		{
			auto& watchdog = instance();
			m_armed = watchdog.m_timeout.count() > 0;
			if (m_armed) { watchdog.arm(operation, plugin); }
		}
		~Scope()
		{
			if (m_armed) { instance().disarm(); }
		}
		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;

	private:
		bool m_armed = false;
	};

private:
	EditorHangWatchdog()
	{
		const QByteArray env = qgetenv("LMMS_VST3_HANG_TIMEOUT");
		bool ok = false;
		const int seconds = env.toInt(&ok);
		m_timeout = std::chrono::seconds{ok && seconds >= 0 ? seconds : 30};
	}

	static EditorHangWatchdog& instance()
	{
		// never destroyed: the thread may still be waiting while the process exits
		static auto* const s_watchdog = new EditorHangWatchdog;
		return *s_watchdog;
	}

	void arm(const char* operation, const vst3::Vst3Plugin* plugin)
	{
		std::lock_guard<std::mutex> lock{m_mutex};
		if (m_depth++ > 0) { return; }
		m_deadline = std::chrono::steady_clock::now() + m_timeout;
		m_operation = operation;
		m_pluginName = plugin ? plugin->name().toStdString() : std::string{};
		// yabridge names the host's socket directory after the bundle, and passes the bundle's
		// Windows counterpart, which has the same name, on the host's command line
		m_hostHint = plugin ? QFileInfo{plugin->classInfo().modulePath}.completeBaseName().toStdString()
			: std::string{};
		if (!m_thread.joinable()) { m_thread = std::thread{[this] { run(); }}; }
		m_wake.notify_one();
	}

	void disarm()
	{
		std::lock_guard<std::mutex> lock{m_mutex};
		--m_depth;
		m_wake.notify_one();
	}

	void run()
	{
		std::unique_lock<std::mutex> lock{m_mutex};
		for (;;)
		{
			m_wake.wait(lock, [this] { return m_depth > 0; });
			const auto deadline = m_deadline;
			const bool done = m_wake.wait_until(lock, deadline,
				[this, deadline] { return m_depth == 0 || m_deadline != deadline; });
			if (!done) { handleHang(lock); }
		}
	}

	void handleHang(std::unique_lock<std::mutex>& lock)
	{
		const std::string operation = m_operation;
		const std::string plugin = m_pluginName;
		const std::string hint = m_hostHint;
		const auto seconds = static_cast<long long>(m_timeout.count());
		lock.unlock();

		std::vector<pid_t> hosts;
		if (!hint.empty())
		{
			for (const pid_t pid : vst3::Vst3StuckHostReaper::yabridgeHosts(getpid(), false))
			{
				if (commandLine(pid).find(hint) != std::string::npos) { hosts.push_back(pid); }
			}
		}

		if (hosts.empty())
		{
			// not bridged, or a group host shared with other plugins: keep waiting, the
			// plugin might still return. Warn once per hung call
			qWarning().nospace() << "VST3: " << plugin.c_str() << " has been in its " << operation.c_str()
				<< " for " << seconds << " s without returning, LMMS is stuck until it does";
			lock.lock();
			const auto deadline = m_deadline;
			m_wake.wait(lock, [this, deadline] { return m_depth == 0 || m_deadline != deadline; });
			return;
		}

		const QString message = QStringLiteral("VST3: %1 hung in its %2 for %3 s, killing its Wine host. "
			"LMMS will exit and save the project to the recovery file")
			.arg(QString::fromStdString(plugin), QString::fromLatin1(operation.c_str())).arg(seconds);
		qCritical().noquote() << message;
		QFile log(ConfigManager::inst()->workingDir() + "crash.log");
		if (log.open(QIODevice::Append | QIODevice::Text))
		{
			QTextStream(&log) << QDateTime::currentDateTime().toString(Qt::ISODate) << " " << message << "\n";
		}
		for (const pid_t pid : hosts) { ::kill(pid, SIGKILL); }

		// yabridge aborts as soon as it reads EOF from the host's socket. Should it not, end LMMS
		// ourselves; if the crash save is already running, it ignores this
		std::this_thread::sleep_for(std::chrono::seconds{5});
		std::abort();
	}

	static std::string commandLine(pid_t pid)
	{
		std::ifstream file{"/proc/" + std::to_string(pid) + "/cmdline", std::ios::binary};
		return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
	}

	std::chrono::seconds m_timeout;
	std::mutex m_mutex;
	std::condition_variable m_wake;
	int m_depth = 0;
	std::chrono::steady_clock::time_point m_deadline;
	std::string m_operation;
	std::string m_pluginName;
	std::string m_hostHint;
	std::thread m_thread;
};
#endif // Q_OS_LINUX

} // namespace


/*
	Vst3EditorWindow
*/

Vst3EditorWindow::Vst3EditorWindow(vst3::Vst3Plugin* plugin) :
	QWidget(nullptr, Qt::Dialog),
	m_plugin(plugin)
{
	setWindowTitle(plugin->name());
	// the dialog window type (_NET_WM_WINDOW_TYPE_DIALOG) keeps tiling
	// window managers like i3 from tiling the editor to the full screen
	setWindowRole("vst3-editor");
	setAttribute(Qt::WA_NativeWindow);
	// the plugin draws the whole window
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAttribute(Qt::WA_NoSystemBackground);

	// watch the QWindow for expose events to complete deferred attaches
	winId();
	windowHandle()->installEventFilter(this);

#ifdef Q_OS_WIN
	// keep Qt's backing store from painting over the plugin's child window
	const auto hwnd = reinterpret_cast<HWND>(winId());
	SetWindowLongPtrW(hwnd, GWL_STYLE, GetWindowLongPtrW(hwnd, GWL_STYLE) | WS_CLIPCHILDREN);
	// moving to a monitor with another DPI changes the scale; plugins
	// answer with resizeView()
	connect(windowHandle(), &QWindow::screenChanged, this, [this]()
	{
		if (m_view && !m_detaching)
		{
			tryPluginCall("editor rescale", [this]() { reportContentScale(m_view.get(), this); });
		}
	});
#endif
}




Vst3EditorWindow::~Vst3EditorWindow()
{
	detachView();
}




tresult PLUGIN_API Vst3EditorWindow::queryInterface(const TUID iid, void** obj)
{
	if (iidEqual(iid, IPlugFrame::iid))
	{
		*obj = static_cast<IPlugFrame*>(this);
		return kResultOk;
	}
#ifdef Q_OS_LINUX
	if (iidEqual(iid, Linux::IRunLoop::iid))
	{
		*obj = static_cast<Linux::IRunLoop*>(this);
		return kResultOk;
	}
#endif
	if (iidEqual(iid, FUnknown::iid))
	{
		*obj = static_cast<IPlugFrame*>(this);
		return kResultOk;
	}
	*obj = nullptr;
	return kNoInterface;
}




QSize Vst3EditorWindow::physicalToLogical(const QSize& size) const
{
#ifdef Q_OS_MACOS
	const qreal scale = 1.; // NSView uses logical points, including on Retina displays.
#else
	const qreal scale = devicePixelRatioF();
#endif
	return {qRound(size.width() / scale), qRound(size.height() / scale)};
}




QSize Vst3EditorWindow::logicalToPhysical(const QSize& size) const
{
#ifdef Q_OS_MACOS
	const qreal scale = 1.; // NSView uses logical points, including on Retina displays.
#else
	const qreal scale = devicePixelRatioF();
#endif
	return {qRound(size.width() * scale), qRound(size.height() * scale)};
}




bool Vst3EditorWindow::tryPluginCall(const char* operation, const std::function<void()>& callback)
{
	QString reason;
#ifdef Q_OS_LINUX
	const EditorHangWatchdog::Scope watchdog{operation, m_plugin};
#endif
	try { callback(); return true; }
	catch (const std::exception& e) { reason = QString::fromUtf8(e.what()); }
	catch (...) { reason = QStringLiteral("unknown exception"); }
	reason = QStringLiteral("%1: %2").arg(QString::fromLatin1(operation), reason);
	qWarning().noquote() << "VST3 editor:" << reason;
	// Defer cleanup/UI work: callbacks may unregister themselves or run
	// during destruction. Qt cancels this work if the window is deleted.
	if (!m_detaching && !m_failureQueued)
	{
		m_failureQueued = true;
		QMetaObject::invokeMethod(this, [this, reason]()
		{
			detachView();
			hide();
			m_failureQueued = false;
			emit editorFailed(reason);
		}, Qt::QueuedConnection);
	}
	return false;
}


QSize Vst3EditorWindow::queryViewSize() const
{
	ViewRect rect{};
	if (m_view->getSize(&rect) != kResultOk || rect.getWidth() <= 0 || rect.getHeight() <= 0) { return {}; }
	return {rect.getWidth(), rect.getHeight()};
}




bool Vst3EditorWindow::attachView()
{
	if (QGuiApplication::platformName() != EditorQpa) { return false; }
	if (m_detaching || m_failureQueued) { return false; }
	if (m_view) { return true; }
	m_view = m_plugin->createEditorView();
	if (!m_view) { return false; }
	bool supported = false;
	const bool success = tryPluginCall("editor setup", [this, &supported]()
	{
		supported = m_view->isPlatformTypeSupported(EditorPlatform) == kResultTrue;
		if (!supported) { return; }
		if (m_view->setFrame(this) != kResultOk)
		{ throw std::runtime_error("plugin rejected its editor frame"); }
#ifndef Q_OS_MACOS
		reportContentScale(m_view.get(), this);
#endif
		// Some editors only know their size once attached() has created them: Kontakt 8 answers
		// kNotInitialized with an empty rect until then. Open at a provisional size and ask again
		// after attaching (or take the plugin's resizeView() during attached())
		const QSize size = queryViewSize();
		m_resizingFromPlugin = true;
		m_viewSize = size;
		resize(physicalToLogical(size.isValid() ? size : ProvisionalEditorSize));
		m_resizingFromPlugin = false;
	});
	if (!success || !supported) { detachView(); return false; }
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
	// child views are positioned relative to their parent here, so there is
	// no window manager placement to wait for
	completeAttach();
	return m_attached;
#else
	m_attachPending = true;
	maybeCompleteAttach(false);
	return true;
#endif
}



void Vst3EditorWindow::maybeCompleteAttach(bool wmPlaced)
{
	if (!m_attachPending || !windowHandle() || !windowHandle()->isExposed()) { return; }

	if (wmPlaced)
	{
		// a move after show means the window manager has placed the window
		m_attachPending = false;
		completeAttach();
		return;
	}

	// exposed, but on some WMs the first expose arrives while the window
	// is still at its pre-placement position - give the placement move a
	// moment to arrive, then attach either way
	QTimer::singleShot(150, this, [this]()
	{
		if (m_attachPending && windowHandle() && windowHandle()->isExposed())
		{
			m_attachPending = false;
			completeAttach();
		}
	});
}




void Vst3EditorWindow::completeAttach()
{
	if (!m_view || m_attached) { return; }
	const bool success = tryPluginCall("editor attachment", [this]()
	{
		if (m_view->attached(reinterpret_cast<void*>(winId()), EditorPlatform) != kResultOk)
		{ throw std::runtime_error("plugin rejected editor attachment"); }
		m_attached = true;
		if (!m_viewSize.isValid())
		{
			// the size was unknown before attaching, and the plugin didn't call resizeView()
			const QSize size = queryViewSize();
			if (!size.isValid()) { throw std::runtime_error("plugin reported an invalid editor size"); }
			m_resizingFromPlugin = true;
			m_viewSize = size;
			resize(physicalToLogical(size));
			m_resizingFromPlugin = false;
		}
		if (m_view->canResize() != kResultTrue)
		{ setFixedSize(physicalToLogical(m_viewSize)); }
	});
	if (!success) { detachView(); return; }

#ifdef Q_OS_LINUX
	// belt and braces for toolkits that update their cached position from
	// synthetic ConfigureNotify events (and again on every move/resize)
	vst3NotifyChildWindowsOfPosition(static_cast<std::uint32_t>(winId()));

	// re-notify periodically while the editor is open: Wine re-creates its
	// window hierarchy lazily and compositing WMs can reparent/move without
	// a client-visible event, both of which would miss one-shot updates
	if (!m_positionRefreshTimer)
	{
		m_positionRefreshTimer = new QTimer(this);
		m_positionRefreshTimer->setInterval(500);
		connect(m_positionRefreshTimer, &QTimer::timeout, this, [this]()
		{
			if (m_attached)
			{
				vst3NotifyChildWindowsOfPosition(static_cast<std::uint32_t>(winId()));
			}
		});
	}
	m_positionRefreshTimer->start();
#endif
}




bool Vst3EditorWindow::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == windowHandle() && event->type() == QEvent::Expose)
	{
		maybeCompleteAttach(false);
	}
	return QWidget::eventFilter(watched, event);
}




void Vst3EditorWindow::detachView()
{
	if (m_detaching) { return; }
	m_detaching = true;
	m_attachPending = false;
	m_resizingFromPlugin = false;
	if (m_positionRefreshTimer) { m_positionRefreshTimer->stop(); }
#ifdef Q_OS_LINUX
	// Stop callbacks first, but retain their objects until removed() has
	// had a chance to unregister them. Cleanup also runs without a view.
	for (auto& entry : m_eventHandlers)
	{
		entry.readNotifier->setEnabled(false);
		entry.writeNotifier->setEnabled(false);
	}
	for (auto& entry : m_timers) { entry.timer->stop(); }
#endif
	const bool attached = std::exchange(m_attached, false);
	auto view = std::move(m_view);
	if (view)
	{
		if (attached) { tryPluginCall("editor removal", [&]() { view->removed(); }); }
		tryPluginCall("editor frame removal", [&]() { view->setFrame(nullptr); });
	}
#ifdef Q_OS_LINUX
	for (auto& entry : m_eventHandlers)
	{
		delete entry.readNotifier;
		delete entry.writeNotifier;
	}
	m_eventHandlers.clear();
	for (auto& entry : m_timers) { delete entry.timer; }
	m_timers.clear();
#endif
	if (auto raw = view.take())
	{ tryPluginCall("editor release", [raw]() { raw->release(); }); }
	m_detaching = false;
}



tresult PLUGIN_API Vst3EditorWindow::resizeView(IPlugView* view, ViewRect* newSize)
{
	if (!view || view != m_view.get() || !newSize || newSize->getWidth() <= 0
		|| newSize->getHeight() <= 0 || m_detaching) { return kInvalidArgument; }
	if (m_resizingFromPlugin) { return kResultFalse; }
	m_resizingFromPlugin = true;
	const bool success = tryPluginCall("editor resize", [&]()
	{
		m_viewSize = QSize(newSize->getWidth(), newSize->getHeight());
		const QSize logical = physicalToLogical(m_viewSize);
		if (m_view->canResize() != kResultTrue) { setFixedSize(logical); }
		else { resize(logical); }
		view->onSize(newSize);
	});
	m_resizingFromPlugin = false;
	return success ? kResultTrue : kResultFalse;
}



void Vst3EditorWindow::moveEvent(QMoveEvent* event)
{
	QWidget::moveEvent(event);
	maybeCompleteAttach(true);
#ifdef Q_OS_LINUX
	if (m_attached)
	{
		// embedded GUIs (Wine/yabridge especially) track their screen
		// position through these synthetic events; without them mouse
		// coordinates go stale whenever the window moves
		vst3NotifyChildWindowsOfPosition(static_cast<std::uint32_t>(winId()));
	}
#endif
}




void Vst3EditorWindow::showEvent(QShowEvent* event)
{
	QWidget::showEvent(event);
#ifdef Q_OS_LINUX
	if (m_attached)
	{
		vst3NotifyChildWindowsOfPosition(static_cast<std::uint32_t>(winId()));
	}
#endif
}




void Vst3EditorWindow::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
#ifdef Q_OS_LINUX
	if (m_attached)
	{
		vst3NotifyChildWindowsOfPosition(static_cast<std::uint32_t>(winId()));
	}
#endif
	if (!m_view || !m_attached || m_resizingFromPlugin) { return; }

	tryPluginCall("editor resize", [this]()
	{
		// only forward real size changes; echoing WM configure events (or
		// fractional-scale rounding drift) back as onSize() makes plugins
		// stretch their editor slightly, which breaks their mouse hit testing
		const QSize physical = logicalToPhysical(size());
		const QSize diff = physical - m_viewSize;
		if ((std::abs(diff.width()) <= 2 && std::abs(diff.height()) <= 2)
			|| m_view->canResize() != kResultTrue)
		{
			return;
		}

		ViewRect rect{0, 0, physical.width(), physical.height()};
		if (m_view->checkSizeConstraint(&rect) == kResultTrue
			|| (rect.getWidth() > 0 && rect.getHeight() > 0))
		{
			m_viewSize = QSize(rect.getWidth(), rect.getHeight());
			m_view->onSize(&rect);
		}
	});
}




void Vst3EditorWindow::closeEvent(QCloseEvent* event)
{
	detachView();
	emit closed();
	QWidget::closeEvent(event);
}




#ifdef Q_OS_LINUX
tresult PLUGIN_API Vst3EditorWindow::registerEventHandler(
	Linux::IEventHandler* handler, Linux::FileDescriptor fd)
{
	if (!handler || m_detaching) { return kInvalidArgument; }

	if (fd < 0 || std::any_of(m_eventHandlers.begin(), m_eventHandlers.end(),
		[fd](const auto& entry) { return entry.fd == fd; })) { return kInvalidArgument; }
	EventHandlerEntry entry;
	entry.handler = handler;
	entry.fd = fd;
	entry.readNotifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
	connect(entry.readNotifier, &QSocketNotifier::activated,
		this, [this, retained = entry.handler, fd]()
		{
			auto handler = retained; // keep alive if it unregisters itself
			tryPluginCall("editor FD callback", [&]() { handler->onFDIsSet(fd); });
		});
	entry.writeNotifier = new QSocketNotifier(fd, QSocketNotifier::Write, this);
	connect(entry.writeNotifier, &QSocketNotifier::activated,
		this, [this, retained = entry.handler, fd]()
		{
			auto handler = retained; // keep alive if it unregisters itself
			tryPluginCall("editor FD callback", [&]() { handler->onFDIsSet(fd); });
		});
	// write readiness is almost always true which would busy-loop; only
	// deliver read events by default like other hosts do
	entry.writeNotifier->setEnabled(false);

	m_eventHandlers.push_back(entry);
	return kResultTrue;
}




tresult PLUGIN_API Vst3EditorWindow::unregisterEventHandler(Linux::IEventHandler* handler)
{
	for (auto it = m_eventHandlers.begin(); it != m_eventHandlers.end(); ++it)
	{
		if (it->handler == handler)
		{
			delete it->readNotifier;
			delete it->writeNotifier;
			m_eventHandlers.erase(it);
			return kResultTrue;
		}
	}
	return kResultFalse;
}




tresult PLUGIN_API Vst3EditorWindow::registerTimer(
	Linux::ITimerHandler* handler, Linux::TimerInterval milliseconds)
{
	if (!handler || m_detaching) { return kInvalidArgument; }

	if (milliseconds == 0 || milliseconds > static_cast<Linux::TimerInterval>(std::numeric_limits<int>::max()))
	{ return kInvalidArgument; }
	TimerEntry entry;
	entry.handler = handler;
	entry.timer = new QTimer(this);
	entry.timer->setInterval(static_cast<int>(std::max<Linux::TimerInterval>(1, milliseconds)));
	connect(entry.timer, &QTimer::timeout, this, [this, retained = entry.handler]()
	{
		auto handler = retained; // keep alive if it unregisters itself
		tryPluginCall("editor timer callback", [&]() { handler->onTimer(); });
	});
	entry.timer->start();

	m_timers.push_back(entry);
	return kResultTrue;
}




tresult PLUGIN_API Vst3EditorWindow::unregisterTimer(Linux::ITimerHandler* handler)
{
	for (auto it = m_timers.begin(); it != m_timers.end(); ++it)
	{
		if (it->handler == handler)
		{
			delete it->timer;
			m_timers.erase(it);
			return kResultTrue;
		}
	}
	return kResultFalse;
}




#endif // Q_OS_LINUX

/*
	Vst3PluginWidget
*/

Vst3PluginWidget::Vst3PluginWidget(vst3::Vst3Plugin* plugin, QWidget* parent,
	bool showEditorControl) :
	QWidget(parent),
	m_plugin(plugin),
	m_showEditorControl(showEditorControl)
{
	buildUi();
}




Vst3PluginWidget::~Vst3PluginWidget()
{
	delete m_editorWindow;
	m_editorWindow = nullptr;
}




void Vst3PluginWidget::buildUi()
{
	auto layout = new QVBoxLayout(this);
	layout->setContentsMargins(4, 4, 4, 4);
	layout->setSpacing(4);

	auto header = new QWidget(this);
	auto headerLayout = new QHBoxLayout(header);
	headerLayout->setContentsMargins(0, 0, 0, 0);

	const auto& info = m_plugin->classInfo();
	auto nameLabel = new QLabel(QString("<b>%1</b> %2").arg(info.name, info.vendor), header);
	headerLayout->addWidget(nameLabel, 1);

	if (m_showEditorControl)
	{
		m_toggleUiButton = new QPushButton(tr("Show GUI"), header);
		m_toggleUiButton->setObjectName("vst3ShowEditor");
		m_toggleUiButton->setCheckable(true);
		connect(m_toggleUiButton, &QPushButton::toggled,
			this, [this](bool checked) { toggleEditor(checked); });
		headerLayout->addWidget(m_toggleUiButton);
	}
	layout->addWidget(header);
	m_pluginErrorLabel = new QLabel(this);
	m_pluginErrorLabel->setObjectName("vst3PluginError");
	m_pluginErrorLabel->setWordWrap(true);
	m_pluginErrorLabel->hide();
	layout->addWidget(m_pluginErrorLabel);
	connect(m_plugin, &vst3::Vst3Plugin::pluginFailed, this,
		[this](const QString& reason)
		{
			m_pluginErrorLabel->setText(tr("Plugin disabled after an error: %1").arg(reason));
			m_pluginErrorLabel->show();
			if (m_toggleUiButton) { m_toggleUiButton->setEnabled(false); }
		});
	if (m_plugin->hasFailed())
	{
		m_pluginErrorLabel->setText(tr("Plugin disabled after an error."));
		m_pluginErrorLabel->show();
		if (m_toggleUiButton) { m_toggleUiButton->setEnabled(false); }
	}
	if (m_showEditorControl)
	{
		m_editorErrorLabel = new QLabel(this);
		m_editorErrorLabel->setWordWrap(true);
		m_editorErrorLabel->hide();
		layout->addWidget(m_editorErrorLabel);
	}

	if (m_plugin->paramCount() == 0)
	{
		layout->addStretch(1);
		return;
	}

	// search box + pager over all parameters
	auto filterRow = new QWidget(this);
	auto filterLayout = new QHBoxLayout(filterRow);
	filterLayout->setContentsMargins(0, 0, 0, 0);
	filterLayout->setSpacing(4);

	m_filterEdit = new QLineEdit(filterRow);
	m_filterEdit->setPlaceholderText(tr("Search %n parameters...", nullptr,
		static_cast<int>(m_plugin->paramCount())));
	m_filterEdit->setClearButtonEnabled(true);
	connect(m_filterEdit, &QLineEdit::textChanged, this, [this](const QString& text)
	{
		m_filter = text;
		m_page = 0;
		rebuildParamPage();
	});
	filterLayout->addWidget(m_filterEdit, 1);

	m_prevPageButton = new QPushButton(QStringLiteral("<"), filterRow);
	m_prevPageButton->setFixedWidth(24);
	connect(m_prevPageButton, &QPushButton::clicked, this, [this]()
	{
		--m_page;
		rebuildParamPage();
	});
	filterLayout->addWidget(m_prevPageButton);

	m_pageLabel = new QLabel(filterRow);
	filterLayout->addWidget(m_pageLabel);

	m_nextPageButton = new QPushButton(QStringLiteral(">"), filterRow);
	m_nextPageButton->setFixedWidth(24);
	connect(m_nextPageButton, &QPushButton::clicked, this, [this]()
	{
		++m_page;
		rebuildParamPage();
	});
	filterLayout->addWidget(m_nextPageButton);

	layout->addWidget(filterRow);

	m_scrollArea = new QScrollArea(this);
	m_scrollArea->setWidgetResizable(true);
	m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_scrollArea->setMinimumHeight(120);
	layout->addWidget(m_scrollArea, 1);

	rebuildParamPage();
}




void Vst3PluginWidget::rebuildParamPage()
{
	//! Knobs shown at once; a page stays fast to create even for plugins
	//! with thousands of parameters
	constexpr std::size_t PageSize = 96;
	constexpr int Columns = 6;

	m_matches.clear();
	const QString needle = m_filter.trimmed();
	for (std::size_t i = 0; i < m_plugin->paramCount(); ++i)
	{
		const auto param = m_plugin->param(i);
		if (needle.isEmpty()
			|| param->title.contains(needle, Qt::CaseInsensitive)
			|| param->shortTitle.contains(needle, Qt::CaseInsensitive))
		{
			m_matches.push_back(i);
		}
	}

	const int pages = std::max<int>(1,
		static_cast<int>((m_matches.size() + PageSize - 1) / PageSize));
	m_page = std::clamp(m_page, 0, pages - 1);

	auto grid = new QWidget(m_scrollArea);
	auto gridLayout = new QGridLayout(grid);
	gridLayout->setContentsMargins(2, 2, 2, 2);
	gridLayout->setSpacing(4);

	const std::size_t begin = static_cast<std::size_t>(m_page) * PageSize;
	const std::size_t end = std::min(begin + PageSize, m_matches.size());
	for (std::size_t i = begin; i < end; ++i)
	{
		const auto param = m_plugin->param(m_matches[i]);
		auto knob = new Knob(KnobType::Bright26, grid);
		knob->setModel(param->model);
		knob->setLabel(param->shortTitle.left(12));
		knob->setHintText(param->title + ": ", " " + param->unit);
		const int cell = static_cast<int>(i - begin);
		gridLayout->addWidget(knob, cell / Columns, cell % Columns, Qt::AlignCenter);
	}
	// keep knobs packed to the top left
	gridLayout->setRowStretch(gridLayout->rowCount(), 1);
	gridLayout->setColumnStretch(Columns, 1);

	// setWidget() deletes the previous grid and its knobs
	m_scrollArea->setWidget(grid);
	m_scrollArea->verticalScrollBar()->setValue(0);

	if (m_matches.empty())
	{
		m_pageLabel->setText(tr("no match"));
	}
	else
	{
		m_pageLabel->setText(tr("%1–%2 of %3")
			.arg(begin + 1).arg(end).arg(m_matches.size()));
	}
	m_prevPageButton->setEnabled(m_page > 0);
	m_nextPageButton->setEnabled(m_page < pages - 1);
}




void Vst3PluginWidget::toggleEditor(bool show)
{
	if (!m_toggleUiButton || !m_editorErrorLabel) { return; }
	if (show)
	{
		m_editorErrorLabel->hide();
#ifdef Q_OS_LINUX
		if (QGuiApplication::platformName() != QStringLiteral("xcb"))
		{
			m_toggleUiButton->setChecked(false);
			m_editorErrorLabel->setText(tr("This plugin editor needs X11/XWayland. "
				"Restart LMMS with QT_QPA_PLATFORM=xcb to open it."));
			m_editorErrorLabel->show();
			return;
		}
#endif
		if (!m_editorWindow)
		{
			m_editorWindow = new Vst3EditorWindow(m_plugin);
			connect(m_editorWindow, &Vst3EditorWindow::editorFailed, this, [this](const QString& reason)
			{
				const QSignalBlocker blocker{m_toggleUiButton};
				m_toggleUiButton->setChecked(false);
				m_toggleUiButton->setText(tr("Show GUI"));
				m_editorErrorLabel->setText(tr("Plugin editor error: %1").arg(reason));
				m_editorErrorLabel->show();
			});
			connect(m_editorWindow, &Vst3EditorWindow::closed, this, [this]()
			{
				if (m_toggleUiButton)
				{
					const QSignalBlocker blocker{m_toggleUiButton};
					m_toggleUiButton->setChecked(false);
					m_toggleUiButton->setText(tr("Show GUI"));
				}
			});
		}
		if (!m_editorWindow->attachView())
		{
			delete m_editorWindow;
			m_editorWindow = nullptr;
			if (m_toggleUiButton)
			{
				m_toggleUiButton->setChecked(false);
				m_editorErrorLabel->setText(tr("The plugin could not open its editor."));
				m_editorErrorLabel->show();
			}
			return;
		}
		m_editorWindow->show();
		m_editorWindow->raise();
		m_toggleUiButton->setText(tr("Hide GUI"));
	}
	else
	{
		m_toggleUiButton->setText(tr("Show GUI"));
		if (m_editorWindow && m_editorWindow->isVisible()) { m_editorWindow->close(); }
	}
}


} // namespace lmms::gui
