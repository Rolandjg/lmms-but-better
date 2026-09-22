/*
 * Lb302.cpp - Incomplete Roland TB-303 bass synth emulation
 *
 * Copyright (c) 2006-2008 Paul Giblock <pgib/at/users.sourceforge.net>
 * Copyright (c) 2026 Fawn Sannar <rubiefawn/at/gmail.com>
 *
 * This file is part of LMMS - https://lmms.io
 *
 * Lb302FilterIIR2 is based on the gsyn filter code by Andy Sloane.
 *
 * Lb302Filter3Pole is based on the TB-303 instrument written by
 * Josep M Comajuncosas for the CSounds library
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

#include "Lb302.h"
#include "Song.h"

#include <cmath>
#include <numbers>

#include <QDebug>
#include <QDomElement>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRandomGenerator>
#include <QTimer>

#include "AutomatableButton.h"
#include "BandLimitedWave.h"
#include "DspEffectLibrary.h"
#include "Engine.h"
#include "embed.h"
#include "InstrumentPlayHandle.h"
#include "InstrumentTrack.h"
#include "ComboBox.h"
#include "FontHelper.h"
#include "Knob.h"
#include "LcdSpinBox.h"
#include "LedCheckBox.h"
#include "NotePlayHandle.h"
#include "Oscillator.h"
#include "PixmapButton.h"
#include "plugin_export.h"

#define LB_24_IGNORE_ENVELOPE
//#define LB_24_RES_TRICK


namespace lmms
{

// Helper to get the phase increment per sample, given a note's frequency and the current sample rate
static inline float phaseInc(float freq) { return freq / Engine::audioEngine()->outputSampleRate(); }

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT lb302_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"LB302",
	QT_TRANSLATE_NOOP("PluginBrowser", "Incomplete monophonic imitation TB-303"),
	"Paul Giblock <pgib/at/users.sf.net>",
	0x0100,
	Plugin::Type::Instrument,
	new PluginPixmapLoader("logo"),
	nullptr,
	nullptr,
};

PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* m, void*)
{
	return new Lb302Synth(static_cast<InstrumentTrack*>(m));
}

} // extern "C"


//
// Lb302Filter
//

void Lb302Filter::recalc()
{
	m_vcf.e[1] = std::exp(6.109f + 1.5876f * fs->envmod + 2.1553f * fs->cutoff - 1.2f * (1.0f - fs->reso));
	m_vcf.e[0] = std::exp(5.613f - 0.8f * fs->envmod + 2.1553f * fs->cutoff - 0.7696f * (1.0f - fs->reso));
	const float pi_sr = std::numbers::pi_v<float> / Engine::audioEngine()->outputSampleRate();
	m_vcf.e[0] *= pi_sr;
	m_vcf.e[1] *= pi_sr;
	m_vcf.e[1] -= m_vcf.e[0];

	m_vcf.resCoeff = std::exp(-1.20f + 3.455f * fs->reso);
};


void Lb302Filter::envRecalc() { m_vcf.c0 *= fs->envdecay; }


void Lb302Filter::playNote() { m_vcf.c0 = m_vcf.e[1]; }


//
// Lb302FilterIIR2
//

Lb302FilterIIR2::Lb302FilterIIR2(Lb302FilterKnobState* p_fs)
	: Lb302Filter(p_fs)
	, m_dist{std::make_unique<DspEffectLibrary::Distortion>(1.f, 1.f)}
{};


void Lb302FilterIIR2::recalc()
{
	Lb302Filter::recalc();
	m_dist->setThreshold(fs->dist * 75.f);
};


void Lb302FilterIIR2::envRecalc()
{
	Lb302Filter::envRecalc();
	const float w = m_vcf.e[0] + m_vcf.c0; // e[0] is adjusted for Hz and doesn't need s_envInc
	const float k = std::exp(-w / m_vcf.resCoeff); // Does this mean c0 is inheritantly?

	m_iir2.a = 2.f * std::cos(2.f * w) * k;
	m_iir2.b = -k * k;
	m_iir2.c = 1.f - m_iir2.a - m_iir2.b;
}


sample_t Lb302FilterIIR2::process(sample_t samp)
{
	sample_t ret = m_iir2.a * m_iir2.d[0] + m_iir2.b * m_iir2.d[1] + m_iir2.c * samp;
	// Delayed samples for filter
	m_iir2.d[1] = m_iir2.d[0];
	m_iir2.d[0] = ret;

	if (fs->dist > 0.f) { ret = m_dist->nextSample(ret); }

	// output = IIR2 + dry
	return ret;
}


//
// Lb302Filter3Pole
//

void Lb302Filter3Pole::recalc()
{
	// DO NOT CALL BASE CLASS
	m_vcf.e[0] = 0.000001f;
	m_vcf.e[1] = 1.f;
}


void Lb302Filter3Pole::envRecalc()
{
	Lb302Filter::envRecalc();

	// e0 is adjusted for Hz and doesn't need s_envInc
	float w = m_vcf.e[0] + m_vcf.c0;
	float k = std::min(fs->cutoff, 0.975f);
	// sampleRateCutoff should not be changed to anything dynamic that is outside the
	// scope of LB302 (like e.g. the audio engine's sample rate) as this changes the filter's cutoff
	// behavior without any modification to its controls.
	constexpr float sampleRateCutoff = 44100.0f;
	float kfco = 50.f + k * (
		(2300.f - 1600.f * fs->envmod)
		+ w * (700.f + 1500.f * k + (1500.f + k * (sampleRateCutoff / 2.f - 6000.f)) * fs->envmod)
	); // + iacc * (0.3f + 0.7f * kfco * kenvmod) * kaccent * kaccurve * 2000.f

#ifdef LB_24_IGNORE_ENVELOPE
	// m_kfcn = fs->cutoff;
	m_kfcn = 2.f * kfco / Engine::audioEngine()->outputSampleRate();
#else
	m_kfcn = w;
#endif
	m_kp = ((-2.7528f * m_kfcn + 3.0429f) * m_kfcn + 1.718f) * m_kfcn - 0.9984f;
	const auto kp1  = m_kp + 1.f;
	m_kp1h = 0.5f * kp1;
#ifdef LB_24_RES_TRICK
	k = std::exp(-w / m_vcf.resCoeff);
	m_kres = k * (((-2.7079f * kp1 + 10.963f) * kp1 - 14.934f) * kp1 + 8.4974f);
#else
	m_kres = fs->reso * (((-2.7079f * kp1 + 10.963f) * kp1 - 14.934f) * kp1 + 8.4974f);
#endif
	m_value = 1.f + (fs->dist * (1.5f + 2.f * m_kres * (1.f - m_kfcn))); // ENVMOD was DIST
}


sample_t Lb302Filter3Pole::process(sample_t samp)
{
	constexpr float volAdjust = 3.f;
	const sample_t ax1 = m_lastin;
	const std::array<sample_t, 2> ay1 = { m_ay[0], m_ay[1] };
	m_lastin = samp - std::tanh(m_kres * m_ay[2]);
	m_ay[0]  = m_kp1h * (m_lastin + ax1)   - m_kp * m_ay[0];
	m_ay[1]  = m_kp1h * (m_ay[0] + ay1[0]) - m_kp * m_ay[1];
	m_ay[2]  = m_kp1h * (m_ay[1] + ay1[1]) - m_kp * m_ay[2];
	return std::tanh(m_ay[2] * m_value) * volAdjust / (1.f + fs->dist);
}


//
// Lb302Synth
//

Lb302Synth::Lb302Synth(InstrumentTrack* instrumentTrack)
	: Instrument(instrumentTrack, &lb302_plugin_descriptor, nullptr, Flag::IsSingleStreamed)
	, m_vcfCutKnob(0.75f, 0.0f, 1.5f, 0.005f, this, tr("VCF Cutoff Frequency"))
	, m_vcfResKnob(0.75f, 0.0f, 1.25f, 0.005f, this, tr("VCF Resonance"))
	, m_vcfModKnob(0.1f, 0.0f, 1.0f, 0.005f, this, tr("VCF Envelope Mod"))
	, m_vcfDecKnob(0.1f, 0.0f, 1.0f, 0.005f, this, tr("VCF Envelope Decay"))
	, m_distKnob(0.0f, 0.0f, 1.0f, 0.01f, this, tr("Distortion"))
	, m_waveShape(8.0f, 0.0f, 11.0f, this, tr("Waveform"))
	, m_slideDecKnob(0.6f, 0.0f, 1.0f, 0.005f, this, tr("Slide Decay"))
	, m_slideToggle(false, this, tr("Slide"))
	, m_accentToggle(false, this, tr("Accent"))
	, m_deadToggle(false, this, tr("Dead"))
	, m_db24Toggle(false, this, tr("24dB/oct Filter"))
	, m_seqEnabled(false, this, tr("Sequencer"))
	, m_seqLength(SeqSteps, 1, SeqSteps, this, tr("Sequence length"))
	, m_seqRate(this, tr("Sequencer rate"))
	, m_seqAccent(0.5f, 0.f, 1.f, 0.01f, this, tr("Accent amount"))
	, m_vcfs{std::make_unique<Lb302FilterIIR2>(&m_fs), std::make_unique<Lb302Filter3Pole>(&m_fs)}
{
	connect(Engine::audioEngine(), &AudioEngine::sampleRateChanged, this, &Lb302Synth::filterChanged);
	connect(&m_vcfCutKnob, &FloatModel::dataChanged, this, &Lb302Synth::filterChanged);
	connect(&m_vcfResKnob, &FloatModel::dataChanged, this, &Lb302Synth::filterChanged);
	connect(&m_vcfModKnob, &FloatModel::dataChanged, this, &Lb302Synth::filterChanged);
	connect(&m_vcfDecKnob, &FloatModel::dataChanged, this, &Lb302Synth::decayChanged);
	connect(&m_db24Toggle, &BoolModel::dataChanged, this, &Lb302Synth::db24Toggled);
	connect(&m_distKnob, &FloatModel::dataChanged, this, &Lb302Synth::filterChanged);

	m_seqRate.addItem(tr("1/8"));
	m_seqRate.addItem(tr("1/16"));
	m_seqRate.addItem(tr("1/32"));
	m_seqRate.setValue(1);

	// A classic acid line to start from: octave jumps, slides and accents
	const std::array<SeqStep, SeqSteps> defaultPattern = {{
		{true, true, false, 0}, {true, false, false, 0}, {true, false, true, 12}, {true, false, false, 0},
		{false, false, false, 0}, {true, true, false, 3}, {true, false, true, 0}, {true, false, false, -2},
		{true, true, false, 0}, {false, false, false, 0}, {true, false, false, 12}, {true, false, true, 10},
		{true, false, false, 0}, {true, true, false, 7}, {false, false, false, 0}, {true, false, true, 5},
	}};
	for (int i = 0; i < SeqSteps; ++i) { m_seqPattern[i].store(packStep(defaultPattern[i])); }

	// db24Toggled() would be called here, but all it does is call
	// recalcFilter(), which is already done in filterChanged(), so there's no
	// need to explicitly call recalcFilter() again.
	filterChanged();
	decayChanged();

	Engine::audioEngine()->addPlayHandle(new InstrumentPlayHandle(this, instrumentTrack));
}


void Lb302Synth::saveSettings(QDomDocument& doc, QDomElement& el)
{
	m_vcfCutKnob.saveSettings(doc, el, "vcf_cut");
	m_vcfResKnob.saveSettings(doc, el, "vcf_res");
	m_vcfModKnob.saveSettings(doc, el, "vcf_mod");
	m_vcfDecKnob.saveSettings(doc, el, "vcf_dec");

	m_waveShape.saveSettings(doc, el, "shape");
	m_distKnob.saveSettings(doc, el, "dist");
	m_slideDecKnob.saveSettings(doc, el, "slide_dec");

	m_slideToggle.saveSettings(doc, el, "slide");
	m_deadToggle.saveSettings(doc, el, "dead");
	m_db24Toggle.saveSettings(doc, el, "db24");

	m_seqEnabled.saveSettings(doc, el, "seq");
	m_seqLength.saveSettings(doc, el, "seq_length");
	m_seqRate.saveSettings(doc, el, "seq_rate");
	m_seqAccent.saveSettings(doc, el, "seq_accent");
	QStringList pattern;
	for (const auto& step : m_seqPattern) { pattern << QString::number(step.load(), 16); }
	el.setAttribute("seq_pattern", pattern.join(','));
}


void Lb302Synth::loadSettings(const QDomElement& el)
{
	m_vcfCutKnob.loadSettings(el, "vcf_cut");
	m_vcfResKnob.loadSettings(el, "vcf_res");
	m_vcfModKnob.loadSettings(el, "vcf_mod");
	m_vcfDecKnob.loadSettings(el, "vcf_dec");

	m_distKnob.loadSettings(el, "dist");
	m_slideDecKnob.loadSettings(el, "slide_dec");
	m_waveShape.loadSettings(el, "shape");
	m_slideToggle.loadSettings(el, "slide");
	m_deadToggle.loadSettings(el, "dead");
	m_db24Toggle.loadSettings(el, "db24");

	// The sequencer is new; older projects keep it switched off and use the default pattern
	m_seqEnabled.loadSettings(el, "seq");
	m_seqLength.loadSettings(el, "seq_length");
	m_seqRate.loadSettings(el, "seq_rate");
	m_seqAccent.loadSettings(el, "seq_accent");
	if (el.hasAttribute("seq_pattern"))
	{
		const auto pattern = el.attribute("seq_pattern").split(',');
		for (int i = 0; i < std::min<int>(SeqSteps, pattern.size()); ++i)
		{
			m_seqPattern[i].store(static_cast<std::uint16_t>(pattern[i].toUInt(nullptr, 16)));
		}
	}

	// db24Toggled() would be called here, but all it does is call
	// recalcFilter(), which is already done in filterChanged(), so there's no
	// need to explicitly call recalcFilter() again.
	filterChanged();
	decayChanged();
}


void Lb302Synth::filterChanged()
{
	m_fs.cutoff = m_vcfCutKnob.value();
	m_fs.reso   = m_vcfResKnob.value();
	m_fs.envmod = m_vcfModKnob.value();
	m_fs.dist   = m_distKnob.value() * s_distRatio;
	recalcFilter();
}


void Lb302Synth::decayChanged()
{
	float d = (2.3f * m_vcfDecKnob.value() + 0.2f) * Engine::audioEngine()->outputSampleRate();
	m_fs.envdecay = std::pow(0.1f, 1.0f / d * s_envInc);
}


void Lb302Synth::db24Toggled() { recalcFilter(); }


QString Lb302Synth::nodeName() const { return lb302_plugin_descriptor.name; }


void Lb302Synth::recalcFilter()
{
	vcf().recalc();
	m_vcfEnvPos = s_envInc; // Trigger filter envelope update in process()
}


void Lb302Synth::process(SampleFrame* outbuf, const f_cnt_t size)
{
	if (m_releaseFrame == 0 || !m_playingNote) { m_vcaMode = VcaMode::Decay; }

	if (m_playingNote)
	{
		constexpr float volRatio = 1.f / DefaultVolume;
		m_noteVolume = m_playingNote->getVolume() * volRatio;
		m_notePan = std::clamp(m_playingNote->getPanning(), PanningLeft, PanningRight);
	}
	const auto vv = panningToVolumeVector(m_notePan, m_noteVolume);

	Lb302Filter& filter = vcf(); // Hold on to the current VCF, and use it throughout this period
	if (m_newFreq)
	{
		m_newFreq = false;
		// Sequencer slides glide into the new pitch without retriggering, like dead notes
		const bool noteIsDead = m_deadToggle.value() || m_seqLegato;
		m_seqLegato = false;
		m_vcoInc = phaseInc(m_trueFreq);

		// Always reset vca on non-dead notes, and only reset vca on decaying (decayed) and never-played
		if (!noteIsDead || (m_vcaMode == VcaMode::Decay || m_vcaMode == VcaMode::NeverPlayed))
		{
			m_vcaMode = VcaMode::Attack;
		}
		else { m_vcaMode = VcaMode::Idle; }

		if (m_slideInc != 0.f)
		{
			// Initiate Slide
			m_slide = m_vcoInc - m_slideInc; // Slide amount
			m_slideBase = m_vcoInc; // The REAL frequency
			m_slideInc = 0.f; // reset from-note
		}
		else { m_slide = 0.f; }

		// Slide-from note, save inc for next note
		// May need to equal m_slideBase + m_slide if last note slid
		if (m_slideToggle.value()) { m_slideInc = m_vcoInc; }

		recalcFilter();
		if (!noteIsDead)
		{
			filter.playNote();
			m_vcfEnvPos = s_envInc; // Ensure envelope is recalculated
		}
	}
	// Note: this has to be computed during processing and cannot be initialized
	// in the constructor because it's dependent on the sample rate and that might
	// change during rendering!
	//
	// At 44.1 kHz this will compute something very close to the previously
	// hard coded value of 0.99897516.
	constexpr auto computeDecayFactor = [](float decayTimeInSeconds, float targetedAttenuation) -> float
	{
		// This is the number of samples that correspond to the decay time in seconds
		auto samplesNeededForDecay = decayTimeInSeconds * Engine::audioEngine()->outputSampleRate();

		// This computes the factor that's needed to make a signal with a value of 1 decay to the
		// targeted attenuation over the time in number of samples.
		return std::pow(targetedAttenuation, 1.f / samplesNeededForDecay);
	};
	constexpr auto gateThreshold = 1.f / 65536.f; // Signal below this value is silenced
	const auto decay = computeDecayFactor(0.245260770975f, gateThreshold);

	const float sampleRatio = 44100.f / Engine::audioEngine()->outputSampleRate();
	for (f_cnt_t i = 0; i < size; ++i)
	{
		// start decay if we're past release
		if (i >= m_releaseFrame) { m_vcaMode = VcaMode::Decay; }

		// update vcf
		if (m_vcfEnvPos >= s_envInc)
		{
			filter.envRecalc();
			m_vcfEnvPos = 0;

			if (m_slide)
			{
				m_vcoInc = m_slideBase - m_slide;
				// Calculate coeff from dec_knob on knob change.
				// TODO: Adjust for s_envInc
				m_slide -= m_slide * (0.1f - m_slideDecKnob.value() * 0.0999f) * sampleRatio;
			}
		}

		m_vcfEnvPos++;

		// update vco
		m_vcoC += m_vcoInc;
		if (m_vcoC > 0.5f) { m_vcoC -= 1.f; }
		m_vcoShape = static_cast<VcoShape>(m_waveShape.value());

		// TODO: Add VCO shape parameters (p0, p1) that changes the shape of
		// each waveform. Merge sawtooths with triangle, and merge square with
		// round square?
		switch (m_vcoShape)
		{
			// p0: curviness of line
			// Is this sawtooth backwards?
			case VcoShape::Sawtooth: m_vcoK = m_vcoC; break;

			// p0: duty rev.saw<->triangle<->saw
			// p1: curviness
			case VcoShape::Triangle:
				m_vcoK = m_vcoC * 2.f + 0.5f;
				if (m_vcoK > 0.5f) { m_vcoK = 1.f - m_vcoK; }
				break;

			// p0: slope of top
			case VcoShape::Square:
				m_vcoK = m_vcoC < 0.f ? 0.5f : -0.5f;
				break;

			// p0: width of round
			case VcoShape::RoundSquare:
				m_vcoK = m_vcoC < 0.f ? std::sqrt(1.f - (m_vcoC * m_vcoC * 4.f)) - 0.5f : -0.5f;
				break;

			// Maybe the fall should be exponential/sinsoidal instead of quadric.
			// [-0.5, 0]: Rise, [0,0.25]: Slope down, [0.25,0.5]: Low
			case VcoShape::Moog:
				m_vcoK = m_vcoC * 2.f + 0.5f;
				if (m_vcoK > 1.f) { m_vcoK = -0.5f; }
				else if (m_vcoK > 0.5f)
				{
					float w = 2.f * (m_vcoK - 0.5f) - 1.f;
					m_vcoK = 0.5f - std::sqrt(1.f - (w * w));
				}
				m_vcoK *= 2.f; // MOOG wave gets filtered away
				break;

			// [-0.5, 0.5] : [-pi, pi]
			case VcoShape::Sine: m_vcoK = 0.5f * Oscillator::sinSample(m_vcoC); break;
			case VcoShape::Exponential: m_vcoK = 0.5f * Oscillator::expSample(m_vcoC); break;
			case VcoShape::WhiteNoise: m_vcoK = 0.5f * Oscillator::noiseSample(m_vcoC); break;

			// The next cases all use the BandLimitedWave class which uses the oscillator increment `m_vcoInc` to compute samples.
			// If that oscillator increment is 0 we return a 0 sample because calling BandLimitedWave::pdToLen(0) leads to a
			// division by 0 which in turn leads to floating point exceptions.
			case VcoShape::BLSawtooth:
				m_vcoK = m_vcoInc == 0.f ? 0.f : BandLimitedWave::oscillate(m_vcoC + 0.5f, BandLimitedWave::pdToLen(m_vcoInc), BandLimitedWave::Waveform::BLSaw) * 0.5f;
				break;

			case VcoShape::BLSquare:
				m_vcoK = m_vcoInc == 0.f ? 0.f : BandLimitedWave::oscillate(m_vcoC + 0.5f, BandLimitedWave::pdToLen(m_vcoInc), BandLimitedWave::Waveform::BLSquare) * 0.5f;
				break;

			case VcoShape::BLTriangle:
				m_vcoK = m_vcoInc == 0.f ? 0.f : BandLimitedWave::oscillate(m_vcoC + 0.5f, BandLimitedWave::pdToLen(m_vcoInc), BandLimitedWave::Waveform::BLTriangle) * 0.5f;
				break;

			case VcoShape::BLMoog:
				m_vcoK = m_vcoInc == 0.f ? 0.f : BandLimitedWave::oscillate(m_vcoC + 0.5f, BandLimitedWave::pdToLen(m_vcoInc), BandLimitedWave::Waveform::BLMoog);
				break;
		}

		// Write out samples.
		sample_t samp = filter.process(m_vcoK) * m_vca;
		for (ch_cnt_t c = 0; c < DEFAULT_CHANNELS; c++) { outbuf[i][c] = samp * vv.vol[c] * m_accentGain; }

		// Handle Envelope
		if (m_vcaMode == VcaMode::Attack)
		{
			m_vca += (s_vcaInitial - m_vca) * s_vcaAttack;
		}
		else if (m_vcaMode == VcaMode::Decay)
		{
			m_vca *= decay;

			// the following line actually speeds up processing
			if (m_vca < gateThreshold)
			{
				m_vca = 0;
				m_vcaMode = VcaMode::NeverPlayed;
			}
		}
	}
}


void Lb302Synth::playNote(NotePlayHandle* nph, SampleFrame*)
{
	if (nph->isMasterNote() || (nph->hasParent() && nph->isReleased())) { return; }

	// Enqueue new note in m_notes
	auto tries = s_maxNoteEnqueueRetries;
	auto writeClaimedExpected = m_notesWriteClaimed.load(std::memory_order_relaxed);
	std::size_t index;
	std::size_t nextIndex;
	for (;;)
	{
		if (!tries--)
		{
			qDebug() << "Lb302: Note dropped due to catastrophically poor performance! This should never happen!";
			return;
		}
		const auto occupied = static_cast<std::ptrdiff_t>(writeClaimedExpected)
			- static_cast<std::ptrdiff_t>(m_notesReadSeq.load(std::memory_order_acquire));
		assert(occupied >= 0);
		// TODO C++23: [[assume(occupied >= 0)]]
		if (static_cast<std::size_t>(occupied) >= s_maxPendingNotes)
		{
			// Queue is full, try again (at least one sender always makes progress)
			busyWaitHint();
			writeClaimedExpected = m_notesWriteClaimed.load(std::memory_order_relaxed);
			continue;
		}
		index = writeClaimedExpected;
		nextIndex = writeClaimedExpected + 1;
		if (m_notesWriteClaimed.compare_exchange_strong(
			writeClaimedExpected,
			nextIndex,
			std::memory_order_acquire)
		) { break; } // Note sent
	}

	m_notes[index & s_notesBufMask] = nph;

	std::size_t writeCommittedExpected = index;
	while (!m_notesWriteCommitted.compare_exchange_strong(writeCommittedExpected, nextIndex, std::memory_order_release))
	{
		writeCommittedExpected = index; // Reset this as the CAS will have changed it
		busyWaitHint();
	}
}


void Lb302Synth::processNote(NotePlayHandle* nph)
{
	/// Start a new note.
	if (nph->m_pluginData != this)
	{
		m_playingNote = nph;
		nph->m_pluginData = this;
		m_newFreq = true;
	}

	m_releaseFrame = std::max(m_releaseFrame, nph->framesLeft() + nph->offset());
	
	if (!m_playingNote && !nph->isReleased() && m_releaseFrame > 0)
	{
		m_playingNote = nph;
		nph->m_pluginData = this;
		if (m_slideToggle.value()) { m_slideInc = phaseInc(nph->frequency()); }
	}

	// Check for slide
	if (m_playingNote == nph)
	{
		// The sequencer owns the pitch; the held note only provides the root for new notes
		if (m_seqEnabled.value())
		{
			if (m_newFreq) { m_trueFreq = nph->frequency(); }
			return;
		}

		m_trueFreq = nph->frequency();
		const auto trueInc = phaseInc(m_trueFreq);
		if (m_slideToggle.value()) { m_slideBase = trueInc; } else { m_vcoInc = trueInc; }
	}
}


void Lb302Synth::play(SampleFrame* working_buffer)
{
	const auto readIdx = m_notesReadSeq.load(std::memory_order_relaxed);
	const auto writeCommitted = m_notesWriteCommitted.load(std::memory_order_acquire);
	// Process notes, but process new notes last
	for (auto i = readIdx; i < writeCommitted; ++i)
	{
		const auto& nph = m_notes[i & s_notesBufMask];
		if (nph->totalFramesPlayed() == 0) { continue; }
		processNote(nph);
	}
	for (auto i = readIdx; i < writeCommitted; ++i)
	{
		const auto& nph = m_notes[i & s_notesBufMask];
		if (nph->totalFramesPlayed() != 0) { continue; }
		processNote(nph);
	}
	// Mark the processed notes as having been read so that playNote() calls can overwrite them
	m_notesReadSeq.fetch_add(writeCommitted - readIdx, std::memory_order_release);

	if (m_seqEnabled.value())
	{
		playSequencer(working_buffer, Engine::audioEngine()->framesPerPeriod());
		return;
	}

	if (m_seqCurrentStep.load(std::memory_order_relaxed) >= 0)
	{
		// Sequencer was just switched off: restore the plain voice
		m_seqCurrentStep.store(-1, std::memory_order_relaxed);
		m_accentGain = 1.f;
		filterChanged();
	}
	process(working_buffer, Engine::audioEngine()->framesPerPeriod());
}




std::uint16_t Lb302Synth::packStep(const SeqStep& step)
{
	return static_cast<std::uint16_t>((step.gate ? 1 : 0) | (step.accent ? 2 : 0) | (step.slide ? 4 : 0)
		| ((std::clamp(step.pitch, -12, 12) + 12) << 8));
}




Lb302Synth::SeqStep Lb302Synth::unpackStep(std::uint16_t packed)
{
	return {(packed & 1) != 0, (packed & 2) != 0, (packed & 4) != 0, std::clamp((packed >> 8) - 12, -12, 12)};
}




Lb302Synth::SeqStep Lb302Synth::seqStep(int index) const
{
	return unpackStep(m_seqPattern[std::clamp(index, 0, SeqSteps - 1)].load(std::memory_order_relaxed));
}




void Lb302Synth::setSeqStep(int index, const SeqStep& step)
{
	m_seqPattern[std::clamp(index, 0, SeqSteps - 1)].store(packStep(step), std::memory_order_relaxed);
}




void Lb302Synth::triggerSeqStep(int index)
{
	const int length = std::clamp(m_seqLength.value(), 1, SeqSteps);
	const auto step = seqStep(index);
	const auto previous = seqStep((index + length - 1) % length);
	m_seqCurrentStep.store(index, std::memory_order_relaxed);

	// Accent: louder and a stronger filter envelope, as on the original
	const float accent = step.gate && step.accent ? m_seqAccent.value() : 0.f;
	m_accentGain = 1.f + accent * 0.8f;
	m_fs.envmod = std::clamp(m_vcfModKnob.value() + accent * 0.6f, 0.f, 1.f);
	recalcFilter();

	if (!step.gate)
	{
		m_seqGateOpen = false;
		if (m_vcaMode != VcaMode::NeverPlayed) { m_vcaMode = VcaMode::Decay; }
		return;
	}

	// A slide on the previous step ties into this one: glide to the new pitch without retriggering
	const bool tied = m_seqGateOpen && previous.gate && previous.slide && m_seqStepIndex > 0;
	m_slideInc = tied ? m_vcoInc : 0.f;
	m_seqLegato = tied;
	m_trueFreq = m_seqRootFreq * std::exp2(step.pitch / 12.f);
	m_newFreq = true;
	m_seqGateOpen = true;
}




void Lb302Synth::playSequencer(SampleFrame* outbuf, f_cnt_t frames)
{
	// A new held note restarts the pattern from its first step, transposed to that note
	if (m_newFreq)
	{
		m_newFreq = false;
		m_seqRootFreq = m_trueFreq;
		m_seqFrames = 0;
		m_seqStepIndex = -1;
		m_seqGateOpen = false;
	}

	constexpr int divisions[] = {8, 16, 32};
	const float sampleRate = Engine::audioEngine()->outputSampleRate();
	const double stepFrames = sampleRate * 60.0 / Engine::getSong()->getTempo()
		* 4.0 / divisions[std::clamp(m_seqRate.value(), 0, 2)];
	const int length = std::clamp(m_seqLength.value(), 1, SeqSteps);

	f_cnt_t pos = 0;
	while (pos < frames)
	{
		if (!m_playingNote || m_playingNote->isReleased())
		{
			// Key released: let the last step ring out
			if (m_vcaMode != VcaMode::NeverPlayed) { m_vcaMode = VcaMode::Decay; }
			m_seqCurrentStep.store(-1, std::memory_order_relaxed);
			m_seqGateOpen = false;
			process(outbuf + pos, frames - pos);
			return;
		}

		const auto stepIndex = static_cast<long>(m_seqFrames / stepFrames);
		if (stepIndex != m_seqStepIndex)
		{
			m_seqStepIndex = stepIndex;
			triggerSeqStep(static_cast<int>(stepIndex % length));
		}

		// Render up to the next event: the end of the gate (55 % of a step, or the whole step
		// when it slides into the next one) or the start of the next step
		const double stepStart = stepIndex * stepFrames;
		auto next = static_cast<f_cnt_t>(std::ceil(stepStart + stepFrames));
		if (m_seqGateOpen)
		{
			const bool slides = seqStep(static_cast<int>(stepIndex % length)).slide;
			const auto gateEnd = static_cast<f_cnt_t>(std::ceil(stepStart + stepFrames * (slides ? 1.0 : 0.55)));
			if (m_seqFrames >= gateEnd)
			{
				m_seqGateOpen = false;
				if (m_vcaMode != VcaMode::NeverPlayed) { m_vcaMode = VcaMode::Decay; }
			}
			else { next = std::min(next, gateEnd); }
		}

		const auto count = std::clamp<f_cnt_t>(next - m_seqFrames, 1, frames - pos);
		process(outbuf + pos, count);
		pos += count;
		m_seqFrames += count;
	}
}


void Lb302Synth::deleteNotePluginData(NotePlayHandle* nph)
{
	if (m_playingNote == nph) { m_playingNote = nullptr; }
}


gui::PluginView* Lb302Synth::instantiateView(QWidget* parent)
{
	return new gui::Lb302SynthView(this, parent);
}


//
// gui::Lb302SynthView
//

namespace gui
{


Lb302SynthView::Lb302SynthView(Instrument* instrument, QWidget* parent)
	: InstrumentView(instrument, parent)
{
	setFixedSize(sizeHint());

	// Sequencer section below the original artwork
	m_seqToggle = new LedCheckBox("", this);
	m_seqToggle->move(52, 262);
	m_seqToggle->setToolTip(tr("Play the step pattern while a key is held (the key sets the root note)"));

	m_seqLengthBox = new LcdSpinBox(2, this, tr("Sequence length"));
	m_seqLengthBox->setLabel(tr("STEPS"));
	m_seqLengthBox->move(80, 253);

	m_seqRateBox = new ComboBox(this);
	m_seqRateBox->setGeometry(122, 259, 62, ComboBox::DEFAULT_HEIGHT);

	m_seqAccentKnob = new Knob(KnobType::Bright26, this);
	m_seqAccentKnob->move(210, 254);
	m_seqAccentKnob->setHintText(tr("Accent:"), "");

	m_seqGrid = new Lb302StepGrid(castModel<Lb302Synth>(), this);
	m_seqGrid->setGeometry(6, 286, 238, 64);

	// GUI
	m_vcfCutKnob = new Knob(KnobType::Bright26, this);
	m_vcfCutKnob->move(75, 130);
	m_vcfCutKnob->setHintText(tr("Cutoff Freq:"), "");

	m_vcfResKnob = new Knob(KnobType::Bright26, this);
	m_vcfResKnob->move(120, 130);
	m_vcfResKnob->setHintText(tr("Resonance:"), "");

	m_vcfModKnob = new Knob(KnobType::Bright26, this);
	m_vcfModKnob->move(165, 130);
	m_vcfModKnob->setHintText(tr("Env Mod:"), "");

	m_vcfDecKnob = new Knob(KnobType::Bright26, this);
	m_vcfDecKnob->move(210, 130);
	m_vcfDecKnob->setHintText(tr("Decay:"), "");

	m_slideToggle = new LedCheckBox("", this);
	m_slideToggle->move(10, 180);

	// accent removed pending real implementation - no need for non-functional buttons
	/* m_accentToggle = new LedCheckBox("", this);
	   m_accentToggle->move(10, 200); */

	m_deadToggle = new LedCheckBox("", this);
	m_deadToggle->move(10, 200);

	m_db24Toggle = new LedCheckBox("", this);
	m_db24Toggle->move(10, 150);
	m_db24Toggle->setToolTip(tr("303-es-que, 24dB/octave, 3 pole filter"));

	m_slideDecKnob = new Knob(KnobType::Bright26, this);
	m_slideDecKnob->move(210, 75);
	m_slideDecKnob->setHintText(tr("Slide Decay:"), "");

	m_distKnob = new Knob(KnobType::Bright26, this);
	m_distKnob->move(210, 190);
	m_distKnob->setHintText(tr("DIST:"), "");

	// Shapes
	const int waveBtnX = 10;
	const int waveBtnY = 96;
	m_waveBtnGrp = new AutomatableButtonGroup(this);

	auto sawWaveBtn = new PixmapButton(this, tr("Saw wave"));
	sawWaveBtn->move(waveBtnX, waveBtnY);
	sawWaveBtn->setActiveGraphic(embed::getIconPixmap("saw_wave_active"));
	sawWaveBtn->setInactiveGraphic(embed::getIconPixmap("saw_wave_inactive"));
	sawWaveBtn->setToolTip(tr("Click here for a sawtooth wave."));
	m_waveBtnGrp->addButton(sawWaveBtn);

	auto triangleWaveBtn = new PixmapButton(this, tr("Triangle wave"));
	triangleWaveBtn->move(waveBtnX + (16 * 1), waveBtnY);
	triangleWaveBtn->setActiveGraphic(embed::getIconPixmap("triangle_wave_active"));
	triangleWaveBtn->setInactiveGraphic(embed::getIconPixmap("triangle_wave_inactive"));
	triangleWaveBtn->setToolTip(tr("Click here for a triangle wave."));
	m_waveBtnGrp->addButton(triangleWaveBtn);

	auto sqrWaveBtn = new PixmapButton(this, tr("Square wave"));
	sqrWaveBtn->move(waveBtnX + (16 * 2), waveBtnY);
	sqrWaveBtn->setActiveGraphic(embed::getIconPixmap("square_wave_active"));
	sqrWaveBtn->setInactiveGraphic(embed::getIconPixmap("square_wave_inactive"));
	sqrWaveBtn->setToolTip(tr("Click here for a square wave."));
	m_waveBtnGrp->addButton(sqrWaveBtn);

	auto roundSqrWaveBtn = new PixmapButton(this, tr("Rounded square wave"));
	roundSqrWaveBtn->move(waveBtnX + (16 * 3), waveBtnY);
	roundSqrWaveBtn->setActiveGraphic( embed::getIconPixmap("round_square_wave_active"));
	roundSqrWaveBtn->setInactiveGraphic( embed::getIconPixmap("round_square_wave_inactive"));
	roundSqrWaveBtn->setToolTip(tr("Click here for a square wave with a rounded end."));
	m_waveBtnGrp->addButton(roundSqrWaveBtn);

	auto moogWaveBtn = new PixmapButton(this, tr("Moog wave"));
	moogWaveBtn->move(waveBtnX + (16 * 4), waveBtnY);
	moogWaveBtn->setActiveGraphic(embed::getIconPixmap("moog_saw_wave_active"));
	moogWaveBtn->setInactiveGraphic(embed::getIconPixmap("moog_saw_wave_inactive"));
	moogWaveBtn->setToolTip(tr("Click here for a moog-like wave."));
	m_waveBtnGrp->addButton(moogWaveBtn);

	auto sinWaveBtn = new PixmapButton(this, tr("Sine wave"));
	sinWaveBtn->move(waveBtnX + (16 * 5), waveBtnY);
	sinWaveBtn->setActiveGraphic(embed::getIconPixmap("sin_wave_active"));
	sinWaveBtn->setInactiveGraphic(embed::getIconPixmap("sin_wave_inactive"));
	sinWaveBtn->setToolTip(tr("Click for a sine wave."));
	m_waveBtnGrp->addButton(sinWaveBtn);

	auto exponentialWaveBtn = new PixmapButton(this, tr("White noise wave"));
	exponentialWaveBtn->move(waveBtnX + (16 * 6), waveBtnY);
	exponentialWaveBtn->setActiveGraphic(embed::getIconPixmap("exp_wave_active"));
	exponentialWaveBtn->setInactiveGraphic(embed::getIconPixmap("exp_wave_inactive"));
	exponentialWaveBtn->setToolTip(tr("Click here for an exponential wave."));
	m_waveBtnGrp->addButton(exponentialWaveBtn);

	auto whiteNoiseWaveBtn = new PixmapButton(this, tr("White noise wave"));
	whiteNoiseWaveBtn->move(waveBtnX + (16 * 7), waveBtnY);
	whiteNoiseWaveBtn->setActiveGraphic(embed::getIconPixmap("white_noise_wave_active"));
	whiteNoiseWaveBtn->setInactiveGraphic(embed::getIconPixmap("white_noise_wave_inactive"));
	whiteNoiseWaveBtn->setToolTip(tr("Click here for white noise."));
	m_waveBtnGrp->addButton(whiteNoiseWaveBtn);

	auto blSawWaveBtn = new PixmapButton(this, tr("Bandlimited saw wave"));
	blSawWaveBtn->move(waveBtnX + (16 * 9) - 8, waveBtnY);
	blSawWaveBtn->setActiveGraphic(embed::getIconPixmap("saw_wave_active"));
	blSawWaveBtn->setInactiveGraphic(embed::getIconPixmap("saw_wave_inactive"));
	blSawWaveBtn->setToolTip(tr("Click here for bandlimited sawtooth wave."));
	m_waveBtnGrp->addButton(blSawWaveBtn);

	auto blSquareWaveBtn = new PixmapButton(this, tr("Bandlimited square wave"));
	blSquareWaveBtn->move(waveBtnX + (16 * 10) - 8, waveBtnY);
	blSquareWaveBtn->setActiveGraphic(embed::getIconPixmap("square_wave_active"));
	blSquareWaveBtn->setInactiveGraphic(embed::getIconPixmap("square_wave_inactive"));
	blSquareWaveBtn->setToolTip(tr("Click here for bandlimited square wave."));
	m_waveBtnGrp->addButton(blSquareWaveBtn);

	auto blTriangleWaveBtn = new PixmapButton(this, tr("Bandlimited triangle wave"));
	blTriangleWaveBtn->move(waveBtnX + (16 * 11) - 8, waveBtnY);
	blTriangleWaveBtn->setActiveGraphic(embed::getIconPixmap("triangle_wave_active"));
	blTriangleWaveBtn->setInactiveGraphic(embed::getIconPixmap("triangle_wave_inactive"));
	blTriangleWaveBtn->setToolTip(tr("Click here for bandlimited triangle wave."));
	m_waveBtnGrp->addButton(blTriangleWaveBtn);

	auto blMoogWaveBtn = new PixmapButton(this, tr("Bandlimited moog saw wave"));
	blMoogWaveBtn->move(waveBtnX + (16 * 12) - 8, waveBtnY);
	blMoogWaveBtn->setActiveGraphic(embed::getIconPixmap("moog_saw_wave_active"));
	blMoogWaveBtn->setInactiveGraphic(embed::getIconPixmap("moog_saw_wave_inactive"));
	blMoogWaveBtn->setToolTip(tr("Click here for bandlimited moog-like wave."));
	m_waveBtnGrp->addButton(blMoogWaveBtn);
}


void Lb302SynthView::modelChanged()
{
	auto syn = castModel<Lb302Synth>();

	m_vcfCutKnob->setModel(&syn->m_vcfCutKnob);
	m_vcfResKnob->setModel(&syn->m_vcfResKnob);
	m_vcfDecKnob->setModel(&syn->m_vcfDecKnob);
	m_vcfModKnob->setModel(&syn->m_vcfModKnob);
	m_slideDecKnob->setModel(&syn->m_slideDecKnob);

	m_distKnob->setModel(&syn->m_distKnob);
	m_waveBtnGrp->setModel(&syn->m_waveShape);

	m_slideToggle->setModel(&syn->m_slideToggle);
	// m_accentToggle->setModel(&syn->accentToggle);
	m_deadToggle->setModel(&syn->m_deadToggle);
	m_db24Toggle->setModel(&syn->m_db24Toggle);

	m_seqToggle->setModel(&syn->m_seqEnabled);
	m_seqLengthBox->setModel(&syn->m_seqLength);
	m_seqRateBox->setModel(&syn->m_seqRate);
	m_seqAccentKnob->setModel(&syn->m_seqAccent);
	connect(&syn->m_seqLength, &Model::dataChanged, m_seqGrid, qOverload<>(&QWidget::update));
}




void Lb302SynthView::paintEvent(QPaintEvent*)
{
	static const auto artwork = PLUGIN_NAME::getIconPixmap("artwork");
	QPainter p(this);
	p.drawPixmap(0, 0, artwork);

	// Continue the brushed grey of the artwork, with the same kind of pink paint splatter
	const QRectF section(0, 250, 250, SeqHeight);
	QLinearGradient grey(0, section.top(), 0, section.bottom());
	grey.setColorAt(0, QColor(214, 215, 219));
	grey.setColorAt(1, QColor(196, 197, 202));
	p.fillRect(section, grey);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);
	QRandomGenerator splatter(302); // fixed seed: the same splatter every time
	for (int i = 0; i < 9; ++i)
	{
		QColor pink(214, 80, 138, 60 + splatter.bounded(90));
		p.setPen(QPen(pink, 1.5 + splatter.bounded(6.0), Qt::SolidLine, Qt::RoundCap));
		QPainterPath stroke;
		const QPointF start(splatter.bounded(250.0), 250 + splatter.bounded(double(SeqHeight)));
		stroke.moveTo(start);
		stroke.cubicTo(start + QPointF(splatter.bounded(160.0) - 80, splatter.bounded(40.0) - 20),
			start + QPointF(splatter.bounded(200.0) - 100, splatter.bounded(60.0) - 30),
			start + QPointF(splatter.bounded(260.0) - 130, splatter.bounded(80.0) - 40));
		p.drawPath(stroke);
		p.setPen(Qt::NoPen);
		p.setBrush(pink);
		p.drawEllipse(start, 2 + splatter.bounded(4.0), 2 + splatter.bounded(4.0));
	}

	// Thin white rule and the section title, like "VCO" and "VCF" above
	p.setPen(QPen(QColor(255, 255, 255, 220), 1));
	p.drawLine(QPointF(0, 250.5), QPointF(250, 250.5));
	auto title = font();
	title.setPixelSize(17);
	p.setFont(title);
	p.setPen(QColor(20, 20, 22));
	p.drawText(QRectF(8, 254, 44, 26), Qt::AlignLeft | Qt::AlignVCenter, tr("SEQ"));
	p.setFont(adjustedToPixelSize(font(), SMALL_FONT_SIZE));
	p.drawText(QRectF(188, 262, 22, 14), Qt::AlignRight | Qt::AlignVCenter, tr("ACC"));
}




Lb302StepGrid::Lb302StepGrid(Lb302Synth* synth, QWidget* parent) :
	QWidget(parent),
	m_synth(synth)
{
	setMouseTracking(true);
	setToolTip(tr("Click a step to switch it on or off, drag up/down to change its pitch, "
		"click A for accent and S to slide into the next step. Right-click resets a step."));
	auto timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, [this]
	{
		const int step = m_synth->currentSeqStep();
		if (step != m_shownStep) { m_shownStep = step; update(); }
	});
	timer->start(30);
}

int Lb302StepGrid::stepAt(int x) const
{
	return std::clamp(x * Lb302Synth::SeqSteps / std::max(1, width()), 0, Lb302Synth::SeqSteps - 1);
}

void Lb302StepGrid::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);

	// Recessed translucent panel over the grey
	p.setPen(QPen(QColor(90, 90, 96), 1));
	p.setBrush(QColor(255, 255, 255, 110));
	p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);

	const double cell = width() / static_cast<double>(Lb302Synth::SeqSteps);
	const int length = castLength();
	auto font = adjustedToPixelSize(this->font(), 8);
	font.setBold(true);
	p.setFont(font);

	for (int i = 0; i < Lb302Synth::SeqSteps; ++i)
	{
		const auto step = m_synth->seqStep(i);
		const double x = i * cell;
		const bool active = i < length;
		const int alpha = active ? 255 : 70;

		// Beat grouping lines every four steps
		if (i > 0)
		{
			p.setPen(QPen(QColor(60, 60, 66, i % 4 == 0 ? 160 : 50), 1));
			p.drawLine(QPointF(x, 2), QPointF(x, height() - 2));
		}

		// Pitch bar: grows up or down from the root line
		const QRectF pitchArea(x + 2, PitchTop, cell - 4, PitchHeight);
		const double mid = pitchArea.center().y();
		p.setPen(QPen(QColor(0, 0, 0, 40), 1));
		p.drawLine(QPointF(pitchArea.left(), mid), QPointF(pitchArea.right(), mid));
		if (step.gate)
		{
			const double h = step.pitch / 12.0 * (PitchHeight / 2.0 - 1);
			const QRectF bar = QRectF(pitchArea.left() + 1, mid - std::max(h, 0.0) - 1.5,
				pitchArea.width() - 2, std::abs(h) + 3);
			p.setPen(Qt::NoPen);
			p.setBrush(QColor(24, 24, 28, alpha));
			p.drawRect(bar);
		}
		if (step.gate && step.pitch != 0)
		{
			p.setPen(QColor(24, 24, 28, alpha));
			p.drawText(QRectF(x, step.pitch > 0 ? mid + 1 : PitchTop, cell, 10), Qt::AlignCenter,
				QString::number(step.pitch));
		}

		// Step LED, lit red while that step plays
		const QPointF led(x + cell / 2, LedRow);
		const bool playing = i == m_shownStep;
		p.setPen(QPen(QColor(40, 40, 44, alpha), 1));
		p.setBrush(playing ? QColor(255, 48, 40) : (step.gate ? QColor(120, 30, 28, alpha) : QColor(60, 60, 64, alpha / 2)));
		p.drawEllipse(led, 3.2, 3.2);
		if (playing)
		{
			QRadialGradient glow(led, 8);
			glow.setColorAt(0, QColor(255, 60, 40, 160));
			glow.setColorAt(1, QColor(255, 60, 40, 0));
			p.setPen(Qt::NoPen);
			p.setBrush(glow);
			p.drawEllipse(led, 8, 8);
		}

		// Accent and slide switches
		auto drawSwitch = [&](double top, bool on, const QString& text, const QColor& onColor)
		{
			const QRectF box(x + 2, top, cell - 4, SwitchHeight);
			p.setPen(QPen(QColor(40, 40, 44, alpha), 1));
			p.setBrush(on ? QColor(onColor.red(), onColor.green(), onColor.blue(), alpha) : QColor(255, 255, 255, alpha / 3));
			p.drawRoundedRect(box, 2, 2);
			p.setPen(on ? QColor(255, 255, 255, alpha) : QColor(40, 40, 44, alpha / 2));
			p.drawText(box, Qt::AlignCenter, text);
		};
		drawSwitch(AccentTop, step.accent, "A", QColor(214, 80, 138));
		drawSwitch(SlideTop, step.slide, "S", QColor(24, 24, 28));
	}
}

int Lb302StepGrid::castLength() const
{
	return std::clamp(m_synth->m_seqLength.value(), 1, Lb302Synth::SeqSteps);
}

void Lb302StepGrid::mousePressEvent(QMouseEvent* event)
{
	const int index = stepAt(event->pos().x());
	auto step = m_synth->seqStep(index);
	m_dragStep = -1;

	if (event->button() == Qt::RightButton)
	{
		step = Lb302Synth::SeqStep{};
	}
	else if (event->pos().y() >= SlideTop) { step.slide = !step.slide; }
	else if (event->pos().y() >= AccentTop) { step.accent = !step.accent; }
	else
	{
		// Pitch area: remember the press so a drag changes pitch and a plain click toggles the gate
		m_dragStep = index;
		m_dragStartY = event->pos().y();
		m_dragStartPitch = step.pitch;
		m_dragged = false;
		return;
	}
	m_synth->setSeqStep(index, step);
	Engine::getSong()->setModified();
	update();
}

void Lb302StepGrid::mouseMoveEvent(QMouseEvent* event)
{
	if (m_dragStep < 0) { return; }
	const int delta = (m_dragStartY - event->pos().y()) / 3;
	if (delta == 0 && !m_dragged) { return; }
	m_dragged = true;
	auto step = m_synth->seqStep(m_dragStep);
	step.gate = true;
	step.pitch = std::clamp(m_dragStartPitch + delta, -12, 12);
	m_synth->setSeqStep(m_dragStep, step);
	update();
}

void Lb302StepGrid::mouseReleaseEvent(QMouseEvent*)
{
	if (m_dragStep >= 0)
	{
		if (!m_dragged)
		{
			auto step = m_synth->seqStep(m_dragStep);
			step.gate = !step.gate;
			m_synth->setSeqStep(m_dragStep, step);
		}
		Engine::getSong()->setModified();
		update();
	}
	m_dragStep = -1;
}


} // namespace gui

} // namespace lmms
