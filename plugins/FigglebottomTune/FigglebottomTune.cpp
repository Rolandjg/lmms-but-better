/*
 * FigglebottomTune.cpp - pitch correction that snaps a voice to a key and scale
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

#include "FigglebottomTune.h"

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

Plugin::Descriptor PLUGIN_EXPORT figglebottomtune_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"FigglebottomTune",
	QT_TRANSLATE_NOOP("PluginBrowser", "Pitch correction: snaps a voice to a key and scale, from subtle to robotic"),
	"LMMS but better",
	0x0100,
	Plugin::Type::Effect,
	new PluginPixmapLoader("logo"),
	nullptr,
	nullptr,
};

}

namespace
{

int maxPeriod(float sampleRate)
{
	return static_cast<int>(std::ceil(sampleRate / figglebottomtune::PitchDetector::MinFrequency));
}

int latencyFor(float sampleRate, int hop)
{
	return 2 * maxPeriod(sampleRate) + 2 * hop;
}

} // namespace


FigglebottomTuneEffect::FigglebottomTuneEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key) :
	Effect(&figglebottomtune_plugin_descriptor, parent, key),
	m_controls(this)
{
	setSampleRate(Engine::audioEngine()->outputSampleRate());
}


f_cnt_t FigglebottomTuneEffect::latency() const
{
	return static_cast<f_cnt_t>(latencyFor(m_sampleRate, m_detector.hop()));
}


void FigglebottomTuneEffect::setSampleRate(float sampleRate)
{
	m_sampleRate = sampleRate;
	m_detector.setSampleRate(sampleRate);
	m_corrector.setTiming(m_detector.hop() / sampleRate, sampleRate);
	m_corrector.reset();
	m_shifter.setup(maxPeriod(sampleRate), latencyFor(sampleRate, m_detector.hop()), m_detector.hop());
	m_controls.m_trace.setHopSeconds(m_detector.hop() / sampleRate);
}


figglebottomtune::CorrectorSettings FigglebottomTuneEffect::settings() const
{
	const auto& c = m_controls;
	figglebottomtune::CorrectorSettings s;
	for (int i = 0; i < figglebottomtune::PitchClasses; ++i) { s.notes[i] = c.m_noteModels[i]->value(); }
	s.retuneSeconds = c.m_retuneModel.value() * 0.001f;
	s.glideSeconds = c.m_glideModel.value() * 0.001f;
	s.amount = c.m_amountModel.value() * 0.01f;
	s.humanize = c.m_humanizeModel.value() * 0.01f;
	s.sticky = c.m_stickyModel.value() * 0.01f;
	s.tolerance = c.m_toleranceModel.value() * 0.01f;
	s.shift = c.m_shiftModel.value() + c.m_fineModel.value() * 0.01f;
	return s;
}


Effect::ProcessStatus FigglebottomTuneEffect::processImpl(SampleFrame* buf, const f_cnt_t frames)
{
	auto& c = m_controls;
	const float sr = Engine::audioEngine()->outputSampleRate();
	if (sr != m_sampleRate) { setSampleRate(sr); }

	// Tracking from strict (0%) to permissive (100%)
	m_detector.setThreshold(0.06f + 0.003f * c.m_trackingModel.value());
	const auto s = settings();
	const float d = dryLevel();
	const float w = wetLevel();
	const float mix = c.m_mixModel.value() * 0.01f;
	const float outGain = dbfsToAmp(c.m_gainModel.value());
	const double markOffset = m_detector.span() * 0.5;

	SampleFrame peak;
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		const SampleFrame in = buf[f];
		if (m_detector.process(0.5f * (in.left() + in.right())))
		{
			const auto corr = m_corrector.process(m_detector.frequency(), s);
			const double time = static_cast<double>(m_shifter.now()) - markOffset;
			m_shifter.pushMark({time, corr.voiced ? corr.period : 0.f, corr.voiced ? corr.ratio : 1.f});
			c.m_trace.push(corr, m_detector.level());
		}
		const SampleFrame wet = m_shifter.process(in);
		const SampleFrame dry = m_shifter.delayedInput();
		const SampleFrame out = (dry * (1.f - mix) + wet * mix) * outGain;
		peak = peak.absMax(out);
		buf[f] = dry * d + out * w;
	}
	c.m_outPeakL = peak.left();
	c.m_outPeakR = peak.right();

	return ProcessStatus::ContinueIfNotQuiet;
}


extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* parent, void* data)
{
	return new FigglebottomTuneEffect(parent, static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data));
}

}

} // namespace lmms
