/*
 * Vst3Instrument.cpp - instrument plugin hosting VST3 instruments
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

#include "Vst3Instrument.h"

#include <QDebug>
#include <QDir>
#include <QDomElement>
#include <QFileDialog>
#include <QGuiApplication>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "AudioEngine.h"
#include "Engine.h"
#include "InstrumentPlayHandle.h"
#include "InstrumentTrack.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "SampleFrame.h"
#include "SubWindow.h"
#include "Vst3Manager.h"
#include "Vst3SubPluginFeatures.h"
#include "Vst3ViewBase.h"

#include "embed.h"
#include "plugin_export.h"

namespace lmms
{


extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT vst3instrument_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"VST3",
	QT_TRANSLATE_NOOP("PluginBrowser",
		"plugin for using arbitrary VST3 instruments inside LMMS."),
	"LMMS developers",
	0x0100,
	Plugin::Type::Instrument,
	new PluginPixmapLoader("logo"),
	"vst3",
	new Vst3SubPluginFeatures(Plugin::Type::Instrument)
};

}




Vst3Instrument::Vst3Instrument(InstrumentTrack* track,
	Descriptor::SubPluginFeatures::Key* key) :
	Instrument(track, &vst3instrument_plugin_descriptor, key,
		Flag::IsSingleStreamed | Flag::IsMidiBased)
{
	const QString uid = key ? key->attributes["uid"] : QString();
	if (!uid.isEmpty())
	{
		m_plugin = std::make_unique<vst3::Vst3Plugin>(this, uid,
			key->attributes["file"]);
	}
	// with an empty uid the instrument starts empty; the user picks a
	// .vst3 file in the instrument view (or loadSettings() restores one)

	connect(Engine::audioEngine(), &AudioEngine::sampleRateChanged,
		this, &Vst3Instrument::onSampleRateChanged);

	// we need a play handle which cares for calling play()
	auto handle = new InstrumentPlayHandle(this, track);
	Engine::audioEngine()->addPlayHandle(handle);
}




Vst3Instrument::~Vst3Instrument()
{
	Engine::audioEngine()->removePlayHandlesOfTypes(instrumentTrack(),
		PlayHandle::Type::NotePlayHandle | PlayHandle::Type::InstrumentPlayHandle);
}




void Vst3Instrument::onSampleRateChanged()
{
	if (m_plugin) { m_plugin->reconfigure(); }
}




void Vst3Instrument::setPlugin(const QString& uid, const QString& file)
{
	// may throw - in that case the current plugin stays untouched
	auto newPlugin = std::make_unique<vst3::Vst3Plugin>(this, uid, file);

	Engine::audioEngine()->requestChangeInModel();
	std::swap(m_plugin, newPlugin);
	Engine::audioEngine()->doneChangeInModel();

	// rebuild views (and their knobs) before the old plugin - and with it
	// the old parameter models - is deleted when newPlugin goes out of scope
	emit pluginChanged();
}




void Vst3Instrument::loadFile(const QString& file)
{
	// allow picking the .so inside a bundle directory; store the bundle path
	QString path = file;
	const int bundlePos = path.indexOf(QLatin1String(".vst3/"));
	if (bundlePos > 0 && path.endsWith(QLatin1String(".so")))
	{
		path = path.left(bundlePos + 5);
	}

	const auto classes = vst3::Vst3Manager::instance()->classesInFile(path);
	if (classes.empty())
	{
#ifdef Q_OS_MACOS
		collectErrorForUI(tr("No compatible macOS VST3 plugin found in \"%1\".").arg(path));
#else
		collectErrorForUI(tr("No VST3 plugin found in \"%1\". For Windows plugins, "
			"register the plugin directory with yabridgectl add and run yabridgectl sync first.").arg(path));
#endif
		return;
	}

	// prefer an instrument class, fall back to the first class
	const vst3::Vst3ClassInfo* chosen = &classes.front();
	for (const auto& info : classes)
	{
		if (info.isInstrument)
		{
			chosen = &info;
			break;
		}
	}

	try
	{
		setPlugin(chosen->uid, chosen->modulePath);
		if (instrumentTrack()) { instrumentTrack()->setName(m_plugin->name()); }
	}
	catch (const std::runtime_error& e)
	{
		collectErrorForUI(tr("Failed to load \"%1\": %2")
			.arg(path, QString::fromUtf8(e.what())));
	}
}




void Vst3Instrument::saveSettings(QDomDocument& doc, QDomElement& elem)
{
	if (m_plugin) { m_plugin->saveSettings(doc, elem); }
}




void Vst3Instrument::loadSettings(const QDomElement& elem)
{
	// instruments created from the "empty" browser entry have no uid in
	// their key; recover the plugin from our own attributes
	if (!m_plugin)
	{
		const QString uid = elem.attribute("uid");
		if (uid.isEmpty()) { return; }
		try
		{
			setPlugin(uid, elem.attribute("plugin-file"));
		}
		catch (const std::runtime_error& e)
		{
			collectErrorForUI(QString::fromUtf8(e.what()));
			return;
		}
	}
	m_plugin->loadSettings(elem);
}




bool Vst3Instrument::handleMidiEvent(const MidiEvent& event, const TimePos& time,
	f_cnt_t offset)
{
	if (m_plugin) { m_plugin->handleMidiInputEvent(event, time, offset); }
	return true;
}




void Vst3Instrument::play(SampleFrame* buf)
{
	const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
	if (m_plugin)
	{
		m_plugin->process(nullptr, buf, frames);
	}
	else
	{
		zeroSampleFrames(buf, frames);
	}
}




gui::PluginView* Vst3Instrument::instantiateView(QWidget* parent)
{
	return new gui::Vst3InsView(this, parent);
}




namespace gui
{


Vst3InsView::Vst3InsView(Vst3Instrument* instrument, QWidget* parent) :
	InstrumentViewFixedSize(instrument, parent)
{
	setAutoFillBackground(true);
	m_layout = new QVBoxLayout(this);
	m_layout->setContentsMargins(20, 16, 20, 16);
	m_layout->setSpacing(8);

	m_pluginLabel = new QLabel(this);
	m_pluginLabel->setAlignment(Qt::AlignCenter);
	m_pluginLabel->setWordWrap(true);
	m_layout->addWidget(m_pluginLabel);
	m_layout->addStretch(1);

	m_openFileButton = new QPushButton(tr("Load VST3 plugin…"), this);
	m_openFileButton->setObjectName("vst3LoadPlugin");
	m_openFileButton->setIcon(embed::getIconPixmap("project_open"));
	m_openFileButton->setMinimumHeight(28);
	m_openFileButton->setToolTip(tr("Select a .vst3 plugin bundle or its native module"));
	connect(m_openFileButton, &QPushButton::clicked,
		this, &Vst3InsView::openFileDialog);
	m_layout->addWidget(m_openFileButton);

	m_toggleUiButton = new QPushButton(tr("Show GUI"), this);
	m_toggleUiButton->setObjectName("vst3ShowEditor");
	m_toggleUiButton->setIcon(embed::getIconPixmap("zoom"));
	m_toggleUiButton->setMinimumHeight(28);
	m_toggleUiButton->setCheckable(true);
	connect(m_toggleUiButton, &QPushButton::toggled,
		this, &Vst3InsView::toggleEditor);
	m_layout->addWidget(m_toggleUiButton);

	m_controlsButton = new QPushButton(tr("Plugin controls"), this);
	m_controlsButton->setObjectName("vst3ShowControls");
	m_controlsButton->setMinimumHeight(28);
	m_controlsButton->setToolTip(tr("Open the plugin parameter knobs in a separate window"));
	connect(m_controlsButton, &QPushButton::clicked,
		this, &Vst3InsView::toggleControls);
	m_layout->addWidget(m_controlsButton);
	m_layout->addStretch(1);

	m_errorLabel = new QLabel(this);
	m_errorLabel->setAlignment(Qt::AlignCenter);
	m_errorLabel->setWordWrap(true);
	m_errorLabel->hide();
	m_layout->addWidget(m_errorLabel);

	connect(instrument, &Vst3Instrument::pluginChanged,
		this, &Vst3InsView::rebuild, Qt::UniqueConnection);
	rebuild();
}




Vst3InsView::~Vst3InsView()
{
	closePluginWindows();
}




void Vst3InsView::openFileDialog()
{
	auto instrument = castModel<Vst3Instrument>();

#ifdef Q_OS_MACOS
	QString dir = QDir::homePath() + "/Library/Audio/Plug-Ins/VST3";
	if (!QDir{dir}.exists()) { dir = "/Library/Audio/Plug-Ins/VST3"; }
#else
	QString dir = QDir::homePath() + "/.vst3";
#endif
	if (!QDir{dir}.exists()) { dir = QDir::homePath(); }

	QFileDialog dialog{this, tr("Open VST3 plugin"), dir,
		tr("VST3 plugins (*.vst3 *.so);;All files (*)")};
	dialog.setFileMode(QFileDialog::ExistingFile);
	// non-native dialog so .vst3 bundle directories can be entered and
	// their inner .so selected on every platform/theme
#ifndef Q_OS_MACOS
	dialog.setOption(QFileDialog::DontUseNativeDialog, true);
#endif
	if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
	{
		return;
	}

	instrument->loadFile(dialog.selectedFiles().first());
}




void Vst3InsView::toggleEditor(bool show)
{
	auto instrument = castModel<Vst3Instrument>();
	auto plugin = instrument ? instrument->plugin() : nullptr;
	if (!plugin)
	{
		const QSignalBlocker blocker{m_toggleUiButton};
		m_toggleUiButton->setChecked(false);
		return;
	}

	if (!show)
	{
		m_toggleUiButton->setText(tr("Show GUI"));
		if (m_editorWindow && m_editorWindow->isVisible()) { m_editorWindow->close(); }
		return;
	}

	m_errorLabel->hide();
#ifndef Q_OS_MACOS
	if (QGuiApplication::platformName() != QStringLiteral("xcb"))
	{
		const QSignalBlocker blocker{m_toggleUiButton};
		m_toggleUiButton->setChecked(false);
		m_errorLabel->setText(tr("This plugin editor needs X11/XWayland. "
			"Restart LMMS with QT_QPA_PLATFORM=xcb to open it."));
		m_errorLabel->show();
		return;
	}

#endif

	if (!m_editorWindow)
	{
		m_editorWindow = new Vst3EditorWindow(plugin);
		connect(m_editorWindow, &Vst3EditorWindow::closed, this, [this]()
		{
			const QSignalBlocker blocker{m_toggleUiButton};
			m_toggleUiButton->setChecked(false);
			m_toggleUiButton->setText(tr("Show GUI"));
		});
	}
	if (!m_editorWindow->attachView())
	{
		delete m_editorWindow;
		m_editorWindow = nullptr;
		const QSignalBlocker blocker{m_toggleUiButton};
		m_toggleUiButton->setChecked(false);
		m_errorLabel->setText(tr("The plugin could not open its editor."));
		m_errorLabel->show();
		return;
	}

	m_editorWindow->show();
	m_editorWindow->raise();
	m_toggleUiButton->setText(tr("Hide GUI"));
}




void Vst3InsView::toggleControls()
{
	auto instrument = castModel<Vst3Instrument>();
	auto plugin = instrument ? instrument->plugin() : nullptr;
	if (!plugin || plugin->paramCount() == 0) { return; }

	if (!m_controlsWindow)
	{
		auto controls = new Vst3PluginWidget(plugin, nullptr, false);
		m_controlsWindow = getGUI()->mainWindow()->addWindowedWidget(controls);
		m_controlsWindow->setAttribute(Qt::WA_DeleteOnClose, false);
		m_controlsWindow->setWindowTitle(instrument->instrumentTrack()->name()
			+ tr(" - VST3 plugin controls"));
		m_controlsWindow->setMinimumSize(440, 260);
		m_controlsWindow->resize(680, 420);
	}

	if (m_controlsWindow->isVisible())
	{
		m_controlsWindow->hide();
	}
	else
	{
		m_controlsWindow->show();
		m_controlsWindow->raise();
	}
}




void Vst3InsView::rebuild()
{
	closePluginWindows();

	auto instrument = castModel<Vst3Instrument>();
	if (instrument && instrument->plugin())
	{
		const auto& info = instrument->plugin()->classInfo();
		m_pluginLabel->setText(QString("<b>%1</b><br><small>%2</small>")
			.arg(info.name.toHtmlEscaped(), info.vendor.toHtmlEscaped()));
		m_toggleUiButton->setEnabled(true);
		m_controlsButton->setEnabled(instrument->plugin()->paramCount() != 0);
	}
	else
	{
		m_pluginLabel->setText(tr("<b>No VST3 plugin loaded</b>"));
		m_toggleUiButton->setEnabled(false);
		m_controlsButton->setEnabled(false);
	}
	m_errorLabel->hide();
}




void Vst3InsView::closePluginWindows()
{
	delete m_editorWindow;
	m_editorWindow = nullptr;
	delete m_controlsWindow;
	m_controlsWindow = nullptr;
	if (m_toggleUiButton)
	{
		const QSignalBlocker blocker{m_toggleUiButton};
		m_toggleUiButton->setChecked(false);
		m_toggleUiButton->setText(tr("Show GUI"));
	}
}




void Vst3InsView::modelChanged()
{
	rebuild();
	connect(castModel<Vst3Instrument>(), &Vst3Instrument::pluginChanged,
		this, &Vst3InsView::rebuild, Qt::UniqueConnection);
}


} // namespace gui


extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* parent, void* data)
{
	using KeyType = Plugin::Descriptor::SubPluginFeatures::Key;
	try
	{
		return new Vst3Instrument(static_cast<InstrumentTrack*>(parent),
			static_cast<KeyType*>(data));
	}
	catch (const std::runtime_error& e)
	{
		qCritical() << "Vst3Instrument:" << e.what();
		return nullptr;
	}
}

}


} // namespace lmms
