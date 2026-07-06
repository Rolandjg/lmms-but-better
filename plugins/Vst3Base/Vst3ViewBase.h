/*
 * Vst3ViewBase.h - GUI classes for the VST3 host
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

#ifndef LMMS_VST3_VIEW_BASE_H
#define LMMS_VST3_VIEW_BASE_H

#include <QWidget>
#include <vector>

#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/gui/iplugview.h"

#include "vst3base_export.h"

class QPushButton;
class QSocketNotifier;
class QTimer;

namespace lmms::vst3
{
class Vst3Plugin;
}

namespace lmms::gui
{


//! Top level window embedding the native VST3 editor. Implements
//! IPlugFrame (resize requests) and Linux::IRunLoop (event loop
//! integration for plugin GUIs).
class VST3BASE_EXPORT Vst3EditorWindow : public QWidget,
	public Steinberg::IPlugFrame,
	public Steinberg::Linux::IRunLoop
{
	Q_OBJECT

public:
	explicit Vst3EditorWindow(vst3::Vst3Plugin* plugin);
	~Vst3EditorWindow() override;

	//! Create and attach the plugin view; false if the plugin has no GUI
	bool attachView();
	void detachView();
	bool isViewAttached() const { return m_view != nullptr; }

	// FUnknown - lifetime is managed by Qt, not by refcounting
	Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override;
	Steinberg::uint32 PLUGIN_API addRef() override { return 1000; }
	Steinberg::uint32 PLUGIN_API release() override { return 1000; }

	// IPlugFrame
	Steinberg::tresult PLUGIN_API resizeView(Steinberg::IPlugView* view,
		Steinberg::ViewRect* newSize) override;

	// Linux::IRunLoop
	Steinberg::tresult PLUGIN_API registerEventHandler(
		Steinberg::Linux::IEventHandler* handler, Steinberg::Linux::FileDescriptor fd) override;
	Steinberg::tresult PLUGIN_API unregisterEventHandler(
		Steinberg::Linux::IEventHandler* handler) override;
	Steinberg::tresult PLUGIN_API registerTimer(
		Steinberg::Linux::ITimerHandler* handler, Steinberg::Linux::TimerInterval milliseconds) override;
	Steinberg::tresult PLUGIN_API unregisterTimer(
		Steinberg::Linux::ITimerHandler* handler) override;

signals:
	void closed();

protected:
	void closeEvent(QCloseEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;

private:
	//! VST3 view coordinates on X11 are physical pixels, Qt widget geometry
	//! is logical (high-DPI scaled) pixels
	QSize physicalToLogical(const QSize& size) const;
	QSize logicalToPhysical(const QSize& size) const;

	vst3::Vst3Plugin* m_plugin;
	Steinberg::IPtr<Steinberg::IPlugView> m_view;
	bool m_resizingFromPlugin = false;

	struct EventHandlerEntry
	{
		Steinberg::Linux::IEventHandler* handler;
		QSocketNotifier* readNotifier;
		QSocketNotifier* writeNotifier;
	};
	struct TimerEntry
	{
		Steinberg::Linux::ITimerHandler* handler;
		QTimer* timer;
	};
	std::vector<EventHandlerEntry> m_eventHandlers;
	std::vector<TimerEntry> m_timers;
};


//! Generic controls for a VST3 plugin: a "show GUI" button plus a grid of
//! knobs for the plugin parameters. Used by the instrument view and the
//! effect controls dialog.
class VST3BASE_EXPORT Vst3PluginWidget : public QWidget
{
	Q_OBJECT

public:
	Vst3PluginWidget(vst3::Vst3Plugin* plugin, QWidget* parent,
		std::size_t maxKnobs = 128);
	~Vst3PluginWidget() override;

	void toggleEditor(bool show);

private:
	void buildUi();

	vst3::Vst3Plugin* m_plugin;
	std::size_t m_maxKnobs;
	Vst3EditorWindow* m_editorWindow = nullptr;
	QPushButton* m_toggleUiButton = nullptr;
};


} // namespace lmms::gui

#endif // LMMS_VST3_VIEW_BASE_H
