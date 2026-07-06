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

#include <cstring>

#include <QCloseEvent>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSocketNotifier>
#include <QTimer>
#include <QVBoxLayout>

#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"

#include "Knob.h"

#include "Vst3Plugin.h"

namespace lmms::gui
{

using namespace Steinberg;

namespace
{

bool iidEqual(const TUID a, const TUID b)
{
	return std::memcmp(a, b, sizeof(TUID)) == 0;
}

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
	if (iidEqual(iid, Linux::IRunLoop::iid))
	{
		*obj = static_cast<Linux::IRunLoop*>(this);
		return kResultOk;
	}
	if (iidEqual(iid, FUnknown::iid))
	{
		*obj = static_cast<IPlugFrame*>(this);
		return kResultOk;
	}
	*obj = nullptr;
	return kNoInterface;
}




bool Vst3EditorWindow::attachView()
{
	if (m_view) { return true; }

	m_view = m_plugin->createEditorView();
	if (!m_view) { return false; }

	if (m_view->isPlatformTypeSupported(kPlatformTypeX11EmbedWindowID) != kResultTrue)
	{
		m_view = nullptr;
		return false;
	}

	m_view->setFrame(this);

	IPlugViewContentScaleSupport* scaleSupport = nullptr;
	if (m_view->queryInterface(IPlugViewContentScaleSupport::iid,
			reinterpret_cast<void**>(&scaleSupport)) == kResultOk && scaleSupport)
	{
		scaleSupport->setContentScaleFactor(
			static_cast<IPlugViewContentScaleSupport::ScaleFactor>(devicePixelRatioF()));
		scaleSupport->release();
	}

	ViewRect size;
	if (m_view->getSize(&size) == kResultOk)
	{
		m_resizingFromPlugin = true;
		resize(size.getWidth(), size.getHeight());
		m_resizingFromPlugin = false;
	}

	if (m_view->attached(reinterpret_cast<void*>(winId()), kPlatformTypeX11EmbedWindowID)
		!= kResultOk)
	{
		m_view->setFrame(nullptr);
		m_view = nullptr;
		return false;
	}

	if (m_view->canResize() != kResultTrue)
	{
		setFixedSize(size.getWidth(), size.getHeight());
	}

	return true;
}




void Vst3EditorWindow::detachView()
{
	if (!m_view) { return; }

	m_view->removed();
	m_view->setFrame(nullptr);
	m_view = nullptr;

	// plugins are supposed to unregister everything in removed(), but
	// don't rely on it
	for (auto& entry : m_eventHandlers)
	{
		delete entry.readNotifier;
		delete entry.writeNotifier;
	}
	m_eventHandlers.clear();
	for (auto& entry : m_timers) { delete entry.timer; }
	m_timers.clear();
}




tresult PLUGIN_API Vst3EditorWindow::resizeView(IPlugView* view, ViewRect* newSize)
{
	if (!view || !newSize) { return kInvalidArgument; }

	m_resizingFromPlugin = true;
	if (m_view && m_view->canResize() != kResultTrue)
	{
		setFixedSize(newSize->getWidth(), newSize->getHeight());
	}
	else
	{
		resize(newSize->getWidth(), newSize->getHeight());
	}
	view->onSize(newSize);
	m_resizingFromPlugin = false;
	return kResultTrue;
}




void Vst3EditorWindow::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	if (m_view && !m_resizingFromPlugin)
	{
		ViewRect rect{0, 0, width(), height()};
		if (m_view->checkSizeConstraint(&rect) == kResultTrue
			|| (rect.getWidth() > 0 && rect.getHeight() > 0))
		{
			m_view->onSize(&rect);
		}
	}
}




void Vst3EditorWindow::closeEvent(QCloseEvent* event)
{
	detachView();
	emit closed();
	QWidget::closeEvent(event);
}




tresult PLUGIN_API Vst3EditorWindow::registerEventHandler(
	Linux::IEventHandler* handler, Linux::FileDescriptor fd)
{
	if (!handler) { return kInvalidArgument; }

	EventHandlerEntry entry;
	entry.handler = handler;
	entry.readNotifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
	connect(entry.readNotifier, &QSocketNotifier::activated,
		this, [handler, fd]() { handler->onFDIsSet(fd); });
	entry.writeNotifier = new QSocketNotifier(fd, QSocketNotifier::Write, this);
	connect(entry.writeNotifier, &QSocketNotifier::activated,
		this, [handler, fd]() { handler->onFDIsSet(fd); });
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
	if (!handler) { return kInvalidArgument; }

	TimerEntry entry;
	entry.handler = handler;
	entry.timer = new QTimer(this);
	entry.timer->setInterval(static_cast<int>(std::max<Linux::TimerInterval>(1, milliseconds)));
	connect(entry.timer, &QTimer::timeout, this, [handler]() { handler->onTimer(); });
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




/*
	Vst3PluginWidget
*/

Vst3PluginWidget::Vst3PluginWidget(vst3::Vst3Plugin* plugin, QWidget* parent,
	std::size_t maxKnobs) :
	QWidget(parent),
	m_plugin(plugin),
	m_maxKnobs(maxKnobs)
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

	m_toggleUiButton = new QPushButton(tr("Show GUI"), header);
	m_toggleUiButton->setCheckable(true);
	connect(m_toggleUiButton, &QPushButton::toggled,
		this, [this](bool checked) { toggleEditor(checked); });
	headerLayout->addWidget(m_toggleUiButton);
	layout->addWidget(header);

	const std::size_t count = std::min(m_plugin->paramCount(), m_maxKnobs);
	if (count > 0)
	{
		auto scrollArea = new QScrollArea(this);
		scrollArea->setWidgetResizable(true);
		scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

		auto grid = new QWidget(scrollArea);
		auto gridLayout = new QGridLayout(grid);
		gridLayout->setContentsMargins(2, 2, 2, 2);
		gridLayout->setSpacing(4);

		constexpr int columns = 6;
		for (std::size_t i = 0; i < count; ++i)
		{
			auto param = m_plugin->param(i);
			auto knob = new Knob(KnobType::Bright26, grid);
			knob->setModel(param->model);
			knob->setLabel(param->shortTitle.left(12));
			knob->setHintText(param->title + ": ", " " + param->unit);
			gridLayout->addWidget(knob,
				static_cast<int>(i) / columns, static_cast<int>(i) % columns,
				Qt::AlignCenter);
		}

		scrollArea->setWidget(grid);
		scrollArea->setMinimumHeight(120);
		layout->addWidget(scrollArea, 1);

		if (m_plugin->paramCount() > m_maxKnobs)
		{
			layout->addWidget(new QLabel(
				tr("(%1 of %2 parameters shown; use the plugin GUI for the rest)")
					.arg(m_maxKnobs).arg(m_plugin->paramCount()), this));
		}
	}
	else
	{
		layout->addStretch(1);
	}
}




void Vst3PluginWidget::toggleEditor(bool show)
{
	if (show)
	{
		if (!m_editorWindow)
		{
			m_editorWindow = new Vst3EditorWindow(m_plugin);
			connect(m_editorWindow, &Vst3EditorWindow::closed, this, [this]()
			{
				if (m_toggleUiButton) { m_toggleUiButton->setChecked(false); }
			});
		}
		if (!m_editorWindow->attachView())
		{
			delete m_editorWindow;
			m_editorWindow = nullptr;
			if (m_toggleUiButton)
			{
				m_toggleUiButton->setChecked(false);
				m_toggleUiButton->setEnabled(false);
				m_toggleUiButton->setText(tr("No GUI"));
			}
			return;
		}
		m_editorWindow->show();
		m_editorWindow->raise();
	}
	else if (m_editorWindow && m_editorWindow->isViewAttached())
	{
		m_editorWindow->close();
	}
}


} // namespace lmms::gui
