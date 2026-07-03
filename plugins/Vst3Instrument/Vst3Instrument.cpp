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
#include <QVBoxLayout>

#include "AudioEngine.h"
#include "Engine.h"
#include "InstrumentPlayHandle.h"
#include "InstrumentTrack.h"
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
	nullptr,
	new Vst3SubPluginFeatures(Plugin::Type::Instrument)
};

}




Vst3Instrument::Vst3Instrument(InstrumentTrack* track,
	Descriptor::SubPluginFeatures::Key* key) :
	Instrument(track, &vst3instrument_plugin_descriptor, key,
		Flag::IsSingleStreamed | Flag::IsMidiBased),
	m_plugin(std::make_unique<vst3::Vst3Plugin>(this,
		key->attributes["uid"], key->attributes["file"]))
{
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
	m_plugin->reconfigure();
}




void Vst3Instrument::saveSettings(QDomDocument& doc, QDomElement& elem)
{
	m_plugin->saveSettings(doc, elem);
}




void Vst3Instrument::loadSettings(const QDomElement& elem)
{
	m_plugin->loadSettings(elem);
}




bool Vst3Instrument::handleMidiEvent(const MidiEvent& event, const TimePos& time,
	f_cnt_t offset)
{
	m_plugin->handleMidiInputEvent(event, time, offset);
	return true;
}




void Vst3Instrument::play(SampleFrame* buf)
{
	m_plugin->process(nullptr, buf, Engine::audioEngine()->framesPerPeriod());
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
	auto layout = new QVBoxLayout(this);
	layout->setContentsMargins(2, 2, 2, 2);
	m_widget = new Vst3PluginWidget(instrument->m_plugin.get(), this);
	layout->addWidget(m_widget);
	setMinimumSize(400, 280);
}




void Vst3InsView::modelChanged()
{
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
