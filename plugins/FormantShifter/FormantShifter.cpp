/*
 * FormantShifter.cpp - formant shifter, formant-preserving pitch shifter and vowel filter
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

#include "FormantShifter.h"

#include <cmath>

#include "AudioEngine.h"
#include "Engine.h"
#include "Song.h"
#include "embed.h"
#include "lmms_math.h"
#include "plugin_export.h"

namespace lmms
{

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT formantshifter_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"Formant Shifter",
	QT_TRANSLATE_NOOP("PluginBrowser", "Formant shifter, formant-preserving pitch shifter, talkbox and vowel filter"),
	"LMMS but better",
	0x0100,
	Plugin::Type::Effect,
	new PixmapLoader("lmms-plugin-logo"),
	nullptr,
	nullptr,
};

}

namespace
{

constexpr std::size_t DryCapacity = formant::SpectralEngine::MaxSize * 2;

//! Saturation in front of the formants: adds harmonics for the vowels to shape (talkbox on a
//! sine or a soft pad). A -12 dBFS signal keeps its level at any setting.
float drive(float x, float amount)
{
	if (amount <= 0.f) { return x; }
	const float g = 1.f + amount * amount * 24.f;
	return std::tanh(x * g) * 0.25f / std::tanh(g * 0.25f);
}

} // namespace


FormantShifterEffect::FormantShifterEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key) :
	Effect(&formantshifter_plugin_descriptor, parent, key),
	m_controls(this),
	m_dry(DryCapacity)
{
}


f_cnt_t FormantShifterEffect::latency() const
{
	return m_controls.mode() == FormantShifterControls::Mode::Spectral
		? static_cast<f_cnt_t>(m_engines[0].latency()) : 0;
}


void FormantShifterEffect::resetState()
{
	for (auto& engine : m_engines) { engine.reset(); }
	for (auto& filter : m_filters) { filter.reset(); }
	m_follower.reset();
	std::fill(m_dry.begin(), m_dry.end(), SampleFrame());
	m_dryWrite = 0;
	m_smoothersPrimed = false;
}


Effect::ProcessStatus FormantShifterEffect::processImpl(SampleFrame* buf, const f_cnt_t frames)
{
	using namespace formant;
	auto& c = m_controls;

	const float sr = Engine::audioEngine()->outputSampleRate();
	if (sr != m_sampleRate)
	{
		m_sampleRate = sr;
		for (auto& engine : m_engines) { engine.setSampleRate(sr); }
		for (auto& filter : m_filters) { filter.setSampleRate(sr); }
		resetState();
	}
	const int mode = c.m_modeModel.value();
	if (mode != m_mode)
	{
		m_mode = mode;
		resetState();
	}
	const bool spectral = c.mode() == FormantShifterControls::Mode::Spectral;
	const std::size_t delay = spectral ? static_cast<std::size_t>(m_engines[0].latency()) : 0;

	const float d = dryLevel();
	const float w = wetLevel();
	const float mix = c.m_mixModel.value() * 0.01f;
	const float outGain = dbfsToAmp(c.m_gainModel.value());
	const float pitchRatio = std::exp2((c.m_pitchModel.value() + c.m_fineModel.value() * 0.01f) / 12.f);
	const bool preserve = c.m_preserveModel.value();
	const int voice = c.m_voiceModel.value();
	const float imprint = c.m_imprintModel.value() * 0.01f;
	const float resonance = c.m_resonanceModel.value();
	const float driveAmount = c.m_driveModel.value() * 0.01f;
	const float spread = c.m_spreadModel.value();
	m_follower.setTimes(0.005f, c.m_envReleaseModel.value() * 0.001f, sr);

	// LFO: synced rates lock to the song position while playing, so wobbles land on the beat
	const auto shape = static_cast<LfoShape>(c.m_lfoShapeModel.value());
	const float syncTicks = c.lfoSyncTicks();
	const auto song = Engine::getSong();
	const double framesPerTick = Engine::framesPerTick();
	const double cyclesPerFrame = syncTicks > 0.f
		? 1.0 / (syncTicks * framesPerTick)
		: c.m_lfoRateModel.value() / sr;
	const bool locked = syncTicks > 0.f && (song->isPlaying() || song->isExporting());
	const double startPhase = locked ? song->periodStartTicks() / syncTicks : m_lfoPhase;

	constexpr f_cnt_t Block = 32;
	// ~15 ms glide for the modulated vowel and formant, which also declicks sample & hold steps
	const float glide = 1.f - std::exp(-static_cast<float>(Block) / (0.015f * sr));

	SampleFrame peak;
	for (f_cnt_t start = 0; start < frames; start += Block)
	{
		const f_cnt_t end = std::min(frames, start + Block);

		const float lfo = lfoValue(shape, startPhase + start * cyclesPerFrame);
		const float level = std::clamp((ampToDbfs(std::max(m_follower.value(), 1e-6f)) + 50.f) / 50.f, 0.f, 1.f);
		const float vowelTarget = std::clamp(c.m_vowelModel.value() + lfo * c.m_lfoVowelModel.value()
			+ level * c.m_envVowelModel.value(), 0.f, static_cast<float>(VowelCount - 1));
		const float formantTarget = c.m_formantModel.value() + lfo * c.m_lfoFormantModel.value();
		if (!m_smoothersPrimed)
		{
			m_vowel = vowelTarget;
			m_formant = formantTarget;
			m_smoothersPrimed = true;
		}
		m_vowel += (vowelTarget - m_vowel) * glide;
		m_formant += (formantTarget - m_formant) * glide;

		const VowelShape vowel = vowelAt(voice, m_vowel);
		for (int ch = 0; ch < 2; ++ch)
		{
			// Spread moves the left formants down and the right ones up
			const float semitones = m_formant + (ch == 0 ? -0.5f : 0.5f) * spread;
			const float ratio = std::exp2(semitones / 12.f);
			if (spectral)
			{
				SpectralEngine::Settings settings;
				settings.formantRatio = preserve ? ratio : ratio * pitchRatio;
				settings.pitchRatio = pitchRatio;
				settings.imprint = imprint;
				// Spectral peaks need to be wider than the sung bandwidths to catch a harmonic or two
				settings.vowel = scaledVowel(vowel, ratio, 2.f / resonance);
				m_engines[ch].setSettings(settings);
			}
			else
			{
				m_filters[ch].set(scaledVowel(vowel, ratio, 1.f), resonance);
			}
		}

		for (f_cnt_t f = start; f < end; ++f)
		{
			const SampleFrame in = buf[f];
			m_follower.process(0.5f * (in.left() + in.right()));

			const float l = drive(in.left(), driveAmount);
			const float r = drive(in.right(), driveAmount);
			const auto wet = spectral
				? SampleFrame(m_engines[0].process(l), m_engines[1].process(r))
				: SampleFrame(m_filters[0].process(l), m_filters[1].process(r));

			m_dry[m_dryWrite] = in;
			const SampleFrame dry = m_dry[(m_dryWrite + DryCapacity - delay) % DryCapacity];
			m_dryWrite = (m_dryWrite + 1) % DryCapacity;

			const SampleFrame out = (dry * (1.f - mix) + wet * mix) * outGain;
			peak = peak.absMax(out);
			c.m_scope.push(0.5f * (dry.left() + dry.right()), 0.5f * (out.left() + out.right()));
			buf[f] = dry * d + out * w;
		}
	}

	m_lfoPhase = std::fmod(startPhase + frames * cyclesPerFrame, 1 << 20);
	c.m_liveVowel.store(m_vowel, std::memory_order_relaxed);
	c.m_liveFormant.store(m_formant, std::memory_order_relaxed);
	c.m_outPeakL = peak.left();
	c.m_outPeakR = peak.right();
	if (spectral) { publishEnvelopes(); }
	else { c.m_envelopeValid.store(false, std::memory_order_relaxed); }

	return ProcessStatus::ContinueIfNotQuiet;
}


void FormantShifterEffect::publishEnvelopes()
{
	const auto frame = m_engines[0].frameCount();
	if (frame == m_publishedFrame) { return; }
	m_publishedFrame = frame;

	auto& c = m_controls;
	const auto& engine = m_engines[0];
	const int bins = engine.bins();
	// FFT magnitudes to dBFS: a full-scale sine reaches size/4 with the Hann window
	const float toDb = 20.f / std::log(10.f);
	const float offset = -20.f * std::log10(engine.size() * 0.25f);
	const auto at = [&](const std::vector<float>& left, const std::vector<float>& right, float bin) {
		const int i = std::clamp(static_cast<int>(bin), 0, bins - 2);
		const float t = std::clamp(bin - i, 0.f, 1.f);
		const float l = left[i] + (left[i + 1] - left[i]) * t;
		const float r = right[i] + (right[i + 1] - right[i]) * t;
		return 0.5f * (l + r) * toDb + offset;
	};
	const float binsPerHz = engine.size() / engine.sampleRate();
	for (int p = 0; p < FormantShifterControls::DisplayPoints; ++p)
	{
		const float bin = FormantShifterControls::displayFrequency(p) * binsPerHz;
		c.m_inputEnvelope[p].store(at(engine.inputEnvelope(), m_engines[1].inputEnvelope(), bin),
			std::memory_order_relaxed);
		c.m_outputEnvelope[p].store(at(engine.outputEnvelope(), m_engines[1].outputEnvelope(), bin),
			std::memory_order_relaxed);
	}
	c.m_envelopeValid.store(true, std::memory_order_release);
}


extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* parent, void* data)
{
	return new FormantShifterEffect(parent, static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data));
}

}

} // namespace lmms
