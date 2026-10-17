/*
 * LoudnessMeter.cpp - EBU R128 loudness meter effect
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

#include "LoudnessMeter.h"

#include "AudioEngine.h"
#include "Engine.h"
#include "Song.h"
#include "embed.h"
#include "plugin_export.h"

namespace lmms
{

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT loudnessmeter_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"Loudness Meter",
	QT_TRANSLATE_NOOP("PluginBrowser", "LUFS loudness, loudness range and true peak (EBU R128)"),
	"LMMS but better",
	0x0100,
	Plugin::Type::Effect,
	new PixmapLoader("lmms-plugin-logo"),
	nullptr,
	nullptr,
};

}


LoudnessMeterEffect::LoudnessMeterEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key) :
	Effect(&loudnessmeter_plugin_descriptor, parent, key),
	m_controls(this)
{
}


void LoudnessMeterEffect::resetMeasurement()
{
	m_analyzer.reset();
	auto& c = m_controls;
	c.m_integrated.store(LoudnessAnalyzer::Silence, std::memory_order_relaxed);
	c.m_range.store(0.f, std::memory_order_relaxed);
	c.m_truePeak.store(0.f, std::memory_order_relaxed);
}


Effect::ProcessStatus LoudnessMeterEffect::processImpl(SampleFrame* buf, const f_cnt_t frames)
{
	auto& c = m_controls;
	const float sr = Engine::audioEngine()->outputSampleRate();
	if (sr != m_sampleRate)
	{
		m_sampleRate = sr;
		m_analyzer.setSampleRate(sr);
	}

	const auto song = Engine::getSong();
	const bool playing = song->isPlaying() || song->isExporting();
	if (c.m_resetRequested.exchange(false) || (playing && !m_wasPlaying && c.m_resetOnPlayModel.value()))
	{
		resetMeasurement();
	}
	m_wasPlaying = playing;

	// A meter: measure the input, pass it through untouched
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		m_analyzer.process(buf[f].left(), buf[f].right());
		if (m_analyzer.takeBlockFlag())
		{
			const auto i = c.m_historyWrite.load(std::memory_order_relaxed);
			c.m_momentaryHistory[i].store(m_analyzer.momentary(), std::memory_order_relaxed);
			c.m_shortTermHistory[i].store(m_analyzer.shortTerm(), std::memory_order_relaxed);
			c.m_historyWrite.store((i + 1) % LoudnessMeterControls::HistorySize, std::memory_order_release);
		}
	}

	c.m_momentary.store(m_analyzer.momentary(), std::memory_order_relaxed);
	c.m_shortTerm.store(m_analyzer.shortTerm(), std::memory_order_relaxed);
	c.m_integrated.store(m_analyzer.integrated(), std::memory_order_relaxed);
	c.m_range.store(m_analyzer.loudnessRange(), std::memory_order_relaxed);
	c.m_truePeak.store(m_analyzer.truePeak(), std::memory_order_relaxed);

	// Keep measuring through silence so the readouts fall back and the history keeps scrolling
	return ProcessStatus::Continue;
}


extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* parent, void* data)
{
	return new LoudnessMeterEffect(parent, static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data));
}

}

} // namespace lmms
