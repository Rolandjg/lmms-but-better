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
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "AudioEngine.h"
#include "Engine.h"
#include "InstrumentPlayHandle.h"
#include "InstrumentTrack.h"
#include "SampleFrame.h"
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
	const QString uid = key->attributes["uid"];
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
		collectErrorForUI(tr("No VST3 plugin found in \"%1\"").arg(path));
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
	InstrumentView(instrument, parent)
{
	setAutoFillBackground(true);
	m_layout = new QVBoxLayout(this);
	m_layout->setContentsMargins(2, 2, 2, 2);
	m_layout->setSpacing(2);

	m_openFileButton = new QPushButton(tr("Open VST3 plugin…"), this);
	m_openFileButton->setToolTip(tr("Select a .vst3 file, or the .so inside "
		"a .vst3 bundle directory"));
	connect(m_openFileButton, &QPushButton::clicked,
		this, &Vst3InsView::openFileDialog);
	m_layout->addWidget(m_openFileButton);

	connect(instrument, &Vst3Instrument::pluginChanged,
		this, &Vst3InsView::rebuild, Qt::UniqueConnection);
	rebuild();

	setMinimumSize(400, 280);
}




void Vst3InsView::openFileDialog()
{
	auto instrument = castModel<Vst3Instrument>();

	QString dir = QDir::homePath() + "/.vst3";
	if (!QDir{dir}.exists()) { dir = QDir::homePath(); }

	QFileDialog dialog{this, tr("Open VST3 plugin"), dir,
		tr("VST3 plugins (*.vst3 *.so);;All files (*)")};
	dialog.setFileMode(QFileDialog::ExistingFile);
	// non-native dialog so .vst3 bundle directories can be entered and
	// their inner .so selected on every platform/theme
	dialog.setOption(QFileDialog::DontUseNativeDialog, true);
	if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
	{
		return;
	}

	instrument->loadFile(dialog.selectedFiles().first());
}




void Vst3InsView::rebuild()
{
	delete m_content;
	m_content = nullptr;

	auto instrument = castModel<Vst3Instrument>();
	if (instrument && instrument->plugin())
	{
		m_content = new Vst3PluginWidget(instrument->plugin(), this);
	}
	else
	{
		auto label = new QLabel(tr("No plugin loaded.\nUse \"Open VST3 "
			"plugin…\" to load one."), this);
		label->setAlignment(Qt::AlignCenter);
		m_content = label;
	}
	m_layout->addWidget(m_content, 1);
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
