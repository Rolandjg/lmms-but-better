/*
 * Amplifier.cpp - A native amplifier effect plugin with sample-exact amplification
 *
 * Copyright (c) 2014 Vesa Kivimäki <contact/dot/diizy/at/nbl/dot/fi>
 * Copyright (c) 2006-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "Amplifier.h"

#include "Engine.h"
#include "AudioEngine.h"
#include "embed.h"
#include "plugin_export.h"

namespace lmms
{

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT amplifier_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"Amplifier",
	QT_TRANSLATE_NOOP("PluginBrowser", "Gain, pan, width, phase and bass-mono utility"),
	"Vesa Kivimäki <contact/dot/diizy/at/nbl/dot/fi>",
	0x0100,
	Plugin::Type::Effect,
	new PixmapLoader("lmms-plugin-logo"),
	nullptr,
	nullptr,
} ;

}


AmplifierEffect::AmplifierEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key) :
	Effect(&amplifier_plugin_descriptor, parent, key),
	m_ampControls(this)
{
}


Effect::ProcessStatus AmplifierEffect::processImpl(SampleFrame* buf, const f_cnt_t frames)
{
	auto& c = m_ampControls;
	const float d = dryLevel();
	const float w = wetLevel();
	const float sr = Engine::audioEngine()->outputSampleRate();

	const ValueBuffer* volumeBuf = c.m_volumeModel.valueBuffer();
	const ValueBuffer* panBuf = c.m_panModel.valueBuffer();
	const ValueBuffer* leftBuf = c.m_leftModel.valueBuffer();
	const ValueBuffer* rightBuf = c.m_rightModel.valueBuffer();
	const ValueBuffer* widthBuf = c.m_widthModel.valueBuffer();

	const auto channelMode = static_cast<AmplifierControls::ChannelMode>(c.m_channelModeModel.value());
	const float invertL = c.m_invertLeftModel.value() ? -1.f : 1.f;
	const float invertR = c.m_invertRightModel.value() ? -1.f : 1.f;
	const bool bassMono = c.m_bassMonoModel.value();
	const bool dcFilter = c.m_dcFilterModel.value();
	for (auto& filter : m_bassSplit) { filter.setCutoff(c.m_bassMonoFreqModel.value(), sr); }
	for (auto& filter : m_dcBlock) { filter.setCutoff(8.f, sr); }

	SampleFrame peak;

	for (f_cnt_t f = 0; f < frames; ++f)
	{
		const float volume = (volumeBuf ? volumeBuf->value(f) : c.m_volumeModel.value()) * 0.01f;
		const float pan = (panBuf ? panBuf->value(f) : c.m_panModel.value()) * 0.01f;
		const float left = (leftBuf ? leftBuf->value(f) : c.m_leftModel.value()) * 0.01f;
		const float right = (rightBuf ? rightBuf->value(f) : c.m_rightModel.value()) * 0.01f;
		const float width = (widthBuf ? widthBuf->value(f) : c.m_widthModel.value()) * 0.01f;

		const float panLeft = std::min(1.0f, 1.0f - pan);
		const float panRight = std::min(1.0f, 1.0f + pan);

		auto& currentFrame = buf[f];
		float l = currentFrame.left();
		float r = currentFrame.right();

		switch (channelMode)
		{
		case AmplifierControls::ChannelMode::Stereo: break;
		case AmplifierControls::ChannelMode::Left: r = l; break;
		case AmplifierControls::ChannelMode::Right: l = r; break;
		case AmplifierControls::ChannelMode::Swap: std::swap(l, r); break;
		}

		l *= invertL;
		r *= invertR;

		if (width != 1.f) { dsp::applyWidth(l, r, width); }

		if (bassMono)
		{
			// Complementary split: highs = input - lows, so the sum reconstructs perfectly
			const float lowL = m_bassSplit[0].lowpass(l);
			const float lowR = m_bassSplit[1].lowpass(r);
			const float lowMono = (lowL + lowR) * 0.5f;
			l = l - lowL + lowMono;
			r = r - lowR + lowMono;
		}

		l *= left * panLeft * volume;
		r *= right * panRight * volume;

		if (dcFilter)
		{
			l = m_dcBlock[0].highpass(l);
			r = m_dcBlock[1].highpass(r);
		}

		const auto s = SampleFrame(l, r);
		peak = peak.absMax(s);
		m_scope.push(l, r);

		// Dry/wet mix
		currentFrame = currentFrame * d + s * w;
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
	return new AmplifierEffect(parent, static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data));
}

}

} // namespace lmms
