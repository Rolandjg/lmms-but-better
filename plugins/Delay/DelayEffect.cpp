/*
 * delayeffect.cpp - definition of the DelayEffect class. The Delay Plugin
 *
 * Copyright (c) 2014 David French <dave/dot/french3/at/googlemail/dot/com>
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

#include "DelayEffect.h"
#include "Engine.h"
#include "embed.h"
#include "lmms_math.h"
#include "plugin_export.h"

namespace lmms
{


extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT delay_plugin_descriptor =
{
	LMMS_STRINGIFY( PLUGIN_NAME ),
	"Delay",
	QT_TRANSLATE_NOOP( "PluginBrowser", "Stereo / ping-pong delay with filtered, saturated feedback" ),
	"Dave French <contact/dot/dave/dot/french3/at/googlemail/dot/com>",
	0x0200,
	Plugin::Type::Effect,
	new PixmapLoader("lmms-plugin-logo"),
	nullptr,
	nullptr,
} ;




DelayEffect::DelayEffect( Model* parent, const Plugin::Descriptor::SubPluginFeatures::Key* key ) :
	Effect( &delay_plugin_descriptor, parent, key ),
	m_delayControls( this )
{
	changeSampleRate();
}




Effect::ProcessStatus DelayEffect::processImpl(SampleFrame* buf, const f_cnt_t frames)
{
	auto& c = m_delayControls;
	const float sr = m_sampleRate;
	const float d = dryLevel();
	const float w = wetLevel();

	const auto mode = static_cast<DelayControls::Mode>(c.m_modeModel.value());
	const float offset = c.m_offsetModel.value() * 0.01f;
	const float lowCut = c.m_lowCutModel.value();
	const float highCut = c.m_highCutModel.value();
	const bool useLowCut = lowCut > 20.5f;
	const bool useHighCut = highCut < 19999.f;
	const float drive = c.m_driveModel.value() * 0.01f;
	const float width = c.m_widthModel.value() * 0.01f;
	const float duck = c.m_duckModel.value() * 0.01f;
	const float freezeTarget = c.m_freezeModel.value() ? 1.f : 0.f;

	for (auto i = 0; i < 2; ++i)
	{
		m_lowCut[i].setCutoff(lowCut, sr);
		m_highCut[i].setCutoff(highCut, sr);
	}

	if (c.m_outGainModel.isValueChanged())
	{
		m_outGain = dbfsToAmp(c.m_outGainModel.value());
	}

	const ValueBuffer* lengthBuffer = c.m_delayTimeModel.valueBuffer();
	const ValueBuffer* feedbackBuffer = c.m_feedbackModel.valueBuffer();
	const ValueBuffer* lfoTimeBuffer = c.m_lfoTimeModel.valueBuffer();
	const ValueBuffer* lfoAmountBuffer = c.m_lfoAmountModel.valueBuffer();

	SampleFrame peak;
	constexpr auto twoPi = 2.0 * std::numbers::pi;

	for (f_cnt_t f = 0; f < frames; ++f)
	{
		auto& frame = buf[f];
		const auto dry = frame;

		const float time = lengthBuffer ? lengthBuffer->value(f) : c.m_delayTimeModel.value();
		const float feedback = feedbackBuffer ? feedbackBuffer->value(f) : c.m_feedbackModel.value();
		const float lfoPeriod = lfoTimeBuffer ? lfoTimeBuffer->value(f) : c.m_lfoTimeModel.value();
		const float lfoDepth = (lfoAmountBuffer ? lfoAmountBuffer->value(f) : c.m_lfoAmountModel.value()) * sr;

		// Quadrature LFO: the right channel is modulated 90 degrees apart for a wider chorus-like image
		m_lfoPhase += twoPi / (std::max(lfoPeriod, 0.001f) * sr);
		if (m_lfoPhase >= twoPi) { m_lfoPhase -= twoPi; }
		const float lfoL = static_cast<float>(std::sin(m_lfoPhase));
		const float lfoR = mode == DelayControls::Mode::Mono ? lfoL : static_cast<float>(std::cos(m_lfoPhase));

		// Delay times glide towards their targets, giving tape-like repitching instead of clicks
		const float timeL = m_timeSmoothers[0].next(time * sr);
		const float timeR = m_timeSmoothers[1].next(time * (1.f + offset) * sr);

		const float freeze = m_freezeSmoother.next(freezeTarget);
		const float fb = feedback + (1.f - feedback) * freeze;
		const float inputGain = 1.f - freeze;

		std::array<float, 2> taps = {
			m_lines[0].read(timeL + lfoDepth * lfoL),
			m_lines[1].read(timeR + lfoDepth * lfoR)
		};
		if (mode == DelayControls::Mode::Mono) { taps[1] = taps[0]; }

		// Color the repeats: filters and saturation only act on the feedback path, and are
		// faded out while frozen so the captured loop doesn't decay
		std::array<float, 2> loop = taps;
		for (auto i = 0; i < 2; ++i)
		{
			float x = loop[i];
			if (useLowCut) { x = m_lowCut[i].highpass(x); }
			if (useHighCut) { x = m_highCut[i].lowpass(x); }
			x = dsp::saturate(x, drive);
			loop[i] = std::clamp(x + (loop[i] - x) * freeze, -8.f, 8.f);
		}

		const float inL = dry.left() * inputGain;
		const float inR = dry.right() * inputGain;
		switch (mode)
		{
		case DelayControls::Mode::Stereo:
			m_lines[0].write(inL + loop[0] * fb);
			m_lines[1].write(inR + loop[1] * fb);
			break;
		case DelayControls::Mode::PingPong:
			// Mono input enters on the left and bounces between the channels
			m_lines[0].write((inL + inR) * 0.5f + loop[1] * fb);
			m_lines[1].write(loop[0] * fb);
			break;
		case DelayControls::Mode::Mono:
			m_lines[0].write((inL + inR) * 0.5f + loop[0] * fb);
			m_lines[1].write(0.f);
			break;
		}

		float wetL = taps[0];
		float wetR = taps[1];
		dsp::applyWidth(wetL, wetR, width);

		// Duck the echoes while the dry signal plays so they don't clutter the source
		const float env = m_duckEnvelope.process(std::max(std::abs(dry.left()), std::abs(dry.right())));
		const float duckGain = 1.f - duck * std::min(1.f, env * 2.f);
		const float gain = m_outGain * duckGain;

		frame = SampleFrame(wetL * gain, wetR * gain);
		peak = peak.absMax(frame);

		frame = dry * d + frame * w;
	}

	c.m_outPeakL = peak.left();
	c.m_outPeakR = peak.right();

	return ProcessStatus::ContinueIfNotQuiet;
}

void DelayEffect::changeSampleRate()
{
	m_sampleRate = Engine::audioEngine()->outputSampleRate();
	for (auto& line : m_lines)
	{
		line.resize(static_cast<std::size_t>(MaxDelaySeconds * m_sampleRate));
	}
	for (auto& smoother : m_timeSmoothers)
	{
		smoother.setTime(0.06f, m_sampleRate);
		smoother.snap(m_delayControls.m_delayTimeModel.value() * m_sampleRate);
	}
	for (auto i = 0; i < 2; ++i)
	{
		m_lowCut[i].reset();
		m_highCut[i].reset();
	}
	m_freezeSmoother.setTime(0.02f, m_sampleRate);
	m_duckEnvelope.setTimes(0.005f, 0.25f, m_sampleRate);
}




extern "C"
{

//needed for getting plugin out of shared lib
PLUGIN_EXPORT Plugin * lmms_plugin_main( Model* parent, void* data )
{
	return new DelayEffect( parent , static_cast<const Plugin::Descriptor::SubPluginFeatures::Key *>( data ) );
}

}}


} // namespace lmms
