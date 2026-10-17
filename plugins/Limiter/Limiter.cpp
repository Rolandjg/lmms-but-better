/*
 * Limiter.cpp - lookahead true-peak brickwall limiter for mastering
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

#include "Limiter.h"

#include <cmath>

#include "AudioEngine.h"
#include "Engine.h"
#include "embed.h"
#include "lmms_math.h"
#include "plugin_export.h"

namespace lmms
{

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT limiter_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"Limiter",
	QT_TRANSLATE_NOOP("PluginBrowser", "Transparent lookahead brickwall limiter with true-peak detection, for mastering"),
	"LMMS but better",
	0x0100,
	Plugin::Type::Effect,
	new PixmapLoader("lmms-plugin-logo"),
	nullptr,
	nullptr,
};

}


LimiterEffect::LimiterEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key) :
	Effect(&limiter_plugin_descriptor, parent, key),
	m_controls(this)
{
}


Effect::ProcessStatus LimiterEffect::processImpl(SampleFrame* buf, const f_cnt_t frames)
{
	auto& c = m_controls;
	const float sr = Engine::audioEngine()->outputSampleRate();
	m_dsp.setSampleRate(sr);
	const float inputGain = dbfsToAmp(c.m_gainModel.value());
	m_dsp.setInputGain(inputGain);
	m_dsp.setCeiling(dbfsToAmp(c.m_ceilingModel.value()));
	m_dsp.setRelease(c.m_releaseModel.value());
	m_dsp.setLookahead(c.m_lookaheadModel.value());
	m_dsp.setStereoLink(c.m_linkModel.value() * 0.01f);
	m_dsp.setTruePeak(c.m_truePeakModel.value());

	// The dry signal gets the same delay as the limited one, so wet/dry mixing does not comb filter
	const auto dryCapacity = static_cast<std::size_t>(LimiterDsp::MaxLookaheadMs * 0.001f * sr) + LimiterDsp::TpDelay + 2;
	if (m_dryDelay.size() != dryCapacity * 2)
	{
		m_dryDelay.assign(dryCapacity * 2, 0.f);
		m_dryWrite = 0;
	}
	const auto latency = static_cast<std::size_t>(m_dsp.latency());

	const float d = dryLevel();
	const float w = wetLevel();
	const int historyFrames = std::max(1, static_cast<int>(LimiterControls::HistoryStep * sr));
	SampleFrame peak;
	float gain = 1.f;
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		auto& frame = buf[f];
		float l = frame.left();
		float r = frame.right();
		const float inPeak = std::max(std::abs(l), std::abs(r)) * inputGain;
		gain = m_dsp.process(l, r);

		m_dryDelay[m_dryWrite * 2] = frame.left();
		m_dryDelay[m_dryWrite * 2 + 1] = frame.right();
		const auto read = (m_dryWrite + dryCapacity - latency) % dryCapacity;
		m_dryWrite = (m_dryWrite + 1) % dryCapacity;
		const auto out = SampleFrame(l, r);
		peak = peak.absMax(out);
		frame = SampleFrame(m_dryDelay[read * 2], m_dryDelay[read * 2 + 1]) * d + out * w;

		m_historyIn = std::max(m_historyIn, inPeak);
		m_historyOut = std::max({m_historyOut, std::abs(l), std::abs(r)});
		m_historyGain = std::min(m_historyGain, gain);
		if (++m_historyFrames >= historyFrames)
		{
			const auto i = c.m_historyWrite.load(std::memory_order_relaxed);
			c.m_historyIn[i].store(m_historyIn, std::memory_order_relaxed);
			c.m_historyOut[i].store(m_historyOut, std::memory_order_relaxed);
			c.m_historyGain[i].store(m_historyGain, std::memory_order_relaxed);
			c.m_historyWrite.store((i + 1) % LimiterControls::HistorySize, std::memory_order_release);
			m_historyFrames = 0;
			m_historyIn = m_historyOut = 0.f;
			m_historyGain = 1.f;
		}
	}

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
	return new LimiterEffect(parent, static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data));
}

}

} // namespace lmms
