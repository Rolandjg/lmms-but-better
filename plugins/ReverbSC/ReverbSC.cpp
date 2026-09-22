/*
 * ReverbSC.cpp - A native reverb based on an algorithm by Sean Costello
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

#include "ReverbSC.h"

#include "Engine.h"
#include "embed.h"
#include "lmms_math.h"
#include "plugin_export.h"

namespace lmms
{


extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT reverbsc_plugin_descriptor =
{
	LMMS_STRINGIFY( PLUGIN_NAME ),
	"ReverbSC",
	QT_TRANSLATE_NOOP( "PluginBrowser", "Lush FDN reverb (Sean Costello) with pre-delay, freeze and ducking" ),
	"Paul Batchelor",
	0x0123,
	Plugin::Type::Effect,
	new PixmapLoader("lmms-plugin-logo"),
	nullptr,
	nullptr,
} ;

}

ReverbSCEffect::ReverbSCEffect( Model* parent, const Descriptor::SubPluginFeatures::Key* key ) :
	Effect( &reverbsc_plugin_descriptor, parent, key ),
	m_reverbSCControls( this )
{
	sp_create(&sp);
	sp->sr = Engine::audioEngine()->outputSampleRate();

	sp_revsc_create(&revsc);
	sp_revsc_init(sp, revsc);

	sp_dcblock_create(&dcblk[0]);
	sp_dcblock_create(&dcblk[1]);

	sp_dcblock_init(sp, dcblk[0], 1);
	sp_dcblock_init(sp, dcblk[1], 1);

	setupHelpers();
}

ReverbSCEffect::~ReverbSCEffect()
{
	sp_revsc_destroy(&revsc);
	sp_dcblock_destroy(&dcblk[0]);
	sp_dcblock_destroy(&dcblk[1]);
	sp_destroy(&sp);
}

Effect::ProcessStatus ReverbSCEffect::processImpl(SampleFrame* buf, const f_cnt_t frames)
{
	auto& c = m_reverbSCControls;
	const float d = dryLevel();
	const float w = wetLevel();
	const float sr = m_sampleRate;

	SPFLOAT tmpL, tmpR;
	SPFLOAT dcblkL, dcblkR;

	ValueBuffer * inGainBuf = c.m_inputGainModel.valueBuffer();
	ValueBuffer * sizeBuf = c.m_sizeModel.valueBuffer();
	ValueBuffer * colorBuf = c.m_colorModel.valueBuffer();
	ValueBuffer * outGainBuf = c.m_outputGainModel.valueBuffer();

	const float lowCut = c.m_lowCutModel.value();
	const bool useLowCut = lowCut > 20.5f;
	for (auto& filter : m_lowCut) { filter.setCutoff(lowCut, sr); }
	const float predelayTarget = c.m_predelayModel.value() * 0.001f * sr;
	const float width = c.m_widthModel.value() * 0.01f;
	const float duck = c.m_duckModel.value() * 0.01f;
	const float freezeTarget = c.m_freezeModel.value() ? 1.f : 0.f;
	// The delay lines are allocated for the default modulation depth, so only reduce it
	revsc->iPitchMod = std::clamp(c.m_modulationModel.value() * 0.01f, 0.f, 1.f);

	SampleFrame peak;

	for( f_cnt_t f = 0; f < frames; ++f )
	{
		const auto dry = buf[f];

		const auto inGain = fastPow10f<SPFLOAT>(
			(inGainBuf ? inGainBuf->values()[f] : c.m_inputGainModel.value()) / 20.f);
		const auto outGain = fastPow10f<SPFLOAT>(
			(outGainBuf ? outGainBuf->values()[f] : c.m_outputGainModel.value()) / 20.f);

		const float freeze = m_freezeSmoother.next(freezeTarget);
		const float predelay = m_predelaySmoother.next(predelayTarget);

		// Input conditioning: gain, low cut and pre-delay
		std::array<float, 2> in = {dry[0] * inGain * (1.f - freeze), dry[1] * inGain * (1.f - freeze)};
		for (auto ch = 0; ch < 2; ++ch)
		{
			if (useLowCut) { in[ch] = m_lowCut[ch].highpass(in[ch]); }
			m_predelay[ch].write(in[ch]);
			if (predelay >= 1.f) { in[ch] = m_predelay[ch].read(predelay); }
		}
		auto s = std::array<SPFLOAT, 2>{in[0], in[1]};

		const float size = sizeBuf ? sizeBuf->values()[f] : c.m_sizeModel.value();
		const float color = colorBuf ? colorBuf->values()[f] : c.m_colorModel.value();
		// Freeze: lossless feedback and an open damping filter keep the tail sustaining
		revsc->feedback = static_cast<SPFLOAT>(size + (0.9999f - size) * freeze);
		revsc->lpfreq = static_cast<SPFLOAT>(color + (sr * 0.45f - color) * freeze);

		sp_revsc_compute(sp, revsc, &s[0], &s[1], &tmpL, &tmpR);
		sp_dcblock_compute(sp, dcblk[0], &tmpL, &dcblkL);
		sp_dcblock_compute(sp, dcblk[1], &tmpR, &dcblkR);

		float wetL = dcblkL;
		float wetR = dcblkR;
		dsp::applyWidth(wetL, wetR, width);

		const float env = m_duckEnvelope.process(std::max(std::abs(dry[0]), std::abs(dry[1])));
		const float gain = outGain * (1.f - duck * std::min(1.f, env * 2.f));
		wetL *= gain;
		wetR *= gain;
		peak = peak.absMax(SampleFrame(wetL, wetR));

		buf[f][0] = d * dry[0] + w * wetL;
		buf[f][1] = d * dry[1] + w * wetR;
	}

	c.m_outPeakL = peak.left();
	c.m_outPeakR = peak.right();

	return ProcessStatus::ContinueIfNotQuiet;
}

void ReverbSCEffect::setupHelpers()
{
	m_sampleRate = Engine::audioEngine()->outputSampleRate();
	for (auto& line : m_predelay) { line.resize(static_cast<std::size_t>(0.55f * m_sampleRate)); }
	for (auto& filter : m_lowCut) { filter.reset(); }
	m_predelaySmoother.setTime(0.05f, m_sampleRate);
	m_predelaySmoother.snap(m_reverbSCControls.m_predelayModel.value() * 0.001f * m_sampleRate);
	m_freezeSmoother.setTime(0.03f, m_sampleRate);
	m_duckEnvelope.setTimes(0.005f, 0.3f, m_sampleRate);
}

void ReverbSCEffect::changeSampleRate()
{
	// Change sr variable in Soundpipe. does not need to be destroyed
	sp->sr = Engine::audioEngine()->outputSampleRate();

	mutex.lock();
	sp_revsc_destroy(&revsc);
	sp_dcblock_destroy(&dcblk[0]);
	sp_dcblock_destroy(&dcblk[1]);

	sp_revsc_create(&revsc);
	sp_revsc_init(sp, revsc);

	sp_dcblock_create(&dcblk[0]);
	sp_dcblock_create(&dcblk[1]);

	sp_dcblock_init(sp, dcblk[0], 1);
	sp_dcblock_init(sp, dcblk[1], 1);
	setupHelpers();
	mutex.unlock();
}

extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin * lmms_plugin_main( Model* parent, void* data )
{
	return new ReverbSCEffect(
		parent,
		static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data)
	);
}

}


} // namespace lmms
