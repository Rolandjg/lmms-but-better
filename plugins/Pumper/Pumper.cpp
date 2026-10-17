/*
 * Pumper.cpp - tempo-synced volume shaper for sidechain style pumping
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

#include "Pumper.h"

#include <cmath>

#include "AudioEngine.h"
#include "Engine.h"
#include "Song.h"
#include "embed.h"
#include "plugin_export.h"

namespace lmms
{

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT pumper_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"Pumper",
	QT_TRANSLATE_NOOP("PluginBrowser", "Tempo-synced volume shaper for sidechain-style pumping"),
	"LMMS but better",
	0x0100,
	Plugin::Type::Effect,
	new PixmapLoader("lmms-plugin-logo"),
	nullptr,
	nullptr,
};

}


PumperEffect::PumperEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key) :
	Effect(&pumper_plugin_descriptor, parent, key),
	m_controls(this)
{
	m_gainSmoother.snap(1.f);
}


Effect::ProcessStatus PumperEffect::processImpl(SampleFrame* buf, const f_cnt_t frames)
{
	auto& c = m_controls;
	const float sr = Engine::audioEngine()->outputSampleRate();
	const float d = dryLevel();
	const float w = wetLevel();
	const auto song = Engine::getSong();

	const double framesPerTick = Engine::framesPerTick();
	const float periodTicks = c.periodTicks();
	const float periodSeconds = static_cast<float>(periodTicks * framesPerTick / sr);
	const float offset = c.m_offsetModel.value() * 0.01f;
	const auto band = static_cast<PumperControls::Band>(c.m_bandModel.value());
	for (auto& filter : m_split) { filter.setCutoff(c.m_crossoverModel.value(), sr); }
	// Just enough smoothing to keep sharp attacks from clicking
	m_gainSmoother.setTime(0.0007f, sr);

	// Lock to the song position while playing, so the dips land on the beat; free-run otherwise
	const bool locked = song->isPlaying() || song->isExporting();
	double startTicks = locked ? song->periodStartTicks() : m_freePhaseTicks;

	SampleFrame peak;
	float phase = 0.f;
	float gain = 1.f;
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		const double ticks = startTicks + f / framesPerTick;
		phase = static_cast<float>(std::fmod(ticks / periodTicks, 1.0)) - offset;
		phase -= std::floor(phase);
		gain = m_gainSmoother.next(c.shapeAt(phase, periodSeconds));

		auto& frame = buf[f];
		float l = frame.left();
		float r = frame.right();
		switch (band)
		{
		case PumperControls::Band::Full:
			l *= gain;
			r *= gain;
			break;
		case PumperControls::Band::Lows:
		case PumperControls::Band::Highs:
		{
			// Complementary split, so with no ducking the bands sum back to the input
			const float lowL = m_split[0].lowpass(l);
			const float lowR = m_split[1].lowpass(r);
			const float highL = l - lowL;
			const float highR = r - lowR;
			if (band == PumperControls::Band::Lows) { l = lowL * gain + highL; r = lowR * gain + highR; }
			else { l = lowL + highL * gain; r = lowR + highR * gain; }
			break;
		}
		}

		const auto out = SampleFrame(l, r);
		peak = peak.absMax(out);
		frame = frame * d + out * w;
	}

	m_freePhaseTicks = std::fmod(startTicks + frames / framesPerTick, static_cast<double>(periodTicks) * 64.0);
	c.m_phase.store(phase, std::memory_order_relaxed);
	c.m_gain.store(gain, std::memory_order_relaxed);
	c.m_outPeakL = peak.left();
	c.m_outPeakR = peak.right();

	return ProcessStatus::ContinueIfNotQuiet;
}


extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* parent, void* data)
{
	return new PumperEffect(parent, static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data));
}

}

} // namespace lmms
