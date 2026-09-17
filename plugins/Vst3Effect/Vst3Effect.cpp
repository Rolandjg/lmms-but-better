/*
 * Vst3Effect.cpp - effect plugin hosting VST3 effects
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

#include "Vst3Effect.h"

#include <QDebug>
#include <QVBoxLayout>

#include "AudioEngine.h"
#include "Engine.h"
#include "SampleFrame.h"
#include "Vst3SubPluginFeatures.h"
#include "Vst3ViewBase.h"

#include "embed.h"
#include "plugin_export.h"

namespace lmms
{


extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT vst3effect_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"VST3",
	QT_TRANSLATE_NOOP("PluginBrowser",
		"plugin for using arbitrary VST3 effects inside LMMS."),
	"LMMS developers",
	0x0100,
	Plugin::Type::Effect,
	new PluginPixmapLoader("logo"),
	nullptr,
	new Vst3SubPluginFeatures(Plugin::Type::Effect)
};

}




/*
	Vst3FxControls
*/

Vst3FxControls::Vst3FxControls(Vst3Effect* effect, const QString& uid,
	const QString& file) :
	EffectControls(effect),
	m_plugin(std::make_unique<vst3::Vst3Plugin>(this, uid, file))
{
}




void Vst3FxControls::saveSettings(QDomDocument& doc, QDomElement& elem)
{
	m_plugin->saveSettings(doc, elem);
}




void Vst3FxControls::loadSettings(const QDomElement& elem)
{
	m_plugin->loadSettings(elem);
}




int Vst3FxControls::controlCount()
{
	return static_cast<int>(m_plugin->paramCount()) + 1; // +1: "show GUI"
}




gui::EffectControlDialog* Vst3FxControls::createView()
{
	return new gui::Vst3FxControlDialog(this);
}




/*
	Vst3Effect
*/

Vst3Effect::Vst3Effect(Model* parent, const Descriptor::SubPluginFeatures::Key* key) :
	Effect(&vst3effect_plugin_descriptor, parent, key),
	m_controls(this, key->attributes["uid"], key->attributes["file"]),
	m_tmpOutput(Engine::audioEngine()->framesPerPeriod())
{
	connect(Engine::audioEngine(), &AudioEngine::sampleRateChanged,
		this, &Vst3Effect::onSampleRateChanged);
}




void Vst3Effect::onSampleRateChanged()
{
	m_controls.m_plugin->reconfigure();
	m_tmpOutput.resize(Engine::audioEngine()->framesPerPeriod());
}




Effect::ProcessStatus Vst3Effect::processImpl(SampleFrame* buf, const f_cnt_t frames)
{
	if (frames > static_cast<f_cnt_t>(m_tmpOutput.size()))
	{
		m_tmpOutput.resize(frames);
	}

	m_controls.m_plugin->process(buf, m_tmpOutput.data(), frames);

	const float d = dryLevel();
	const float w = wetLevel();
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		buf[f][0] = d * buf[f][0] + w * m_tmpOutput[f][0];
		buf[f][1] = d * buf[f][1] + w * m_tmpOutput[f][1];
	}

	return ProcessStatus::ContinueIfNotQuiet;
}




/*
	Vst3FxControlDialog
*/

namespace gui
{

Vst3FxControlDialog::Vst3FxControlDialog(Vst3FxControls* controls) :
	EffectControlDialog(controls)
{
	auto layout = new QVBoxLayout(this);
	layout->setContentsMargins(2, 2, 2, 2);
	m_widget = new Vst3PluginWidget(controls->plugin(), this);
	layout->addWidget(m_widget);
	setMinimumSize(400, 280);
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
		return new Vst3Effect(parent, static_cast<const KeyType*>(data));
	}
	catch (const std::runtime_error& e)
	{
		qCritical() << "Vst3Effect:" << e.what();
		return nullptr;
	}
}

}


} // namespace lmms
