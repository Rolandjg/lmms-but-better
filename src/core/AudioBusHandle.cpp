/*
 * AudioBusHandle.cpp - ThreadableJob between PlayHandle and MixerChannel
 *
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 * Copyright (c) 2025 Johannes Lorenz <jlsf2013$users.sourceforge.net, $=@>
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

#include "AudioBusHandle.h"

#include <QMutexLocker>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "EffectChain.h"
#include "Mixer.h"
#include "Engine.h"
#include "MixHelpers.h"

namespace lmms
{

AudioBusHandle::AudioBusHandle(const QString& name, bool hasEffectChain,
	FloatModel* volumeModel, FloatModel* panningModel,
	BoolModel* mutedModel) :
	m_bufferUsage(false),
	m_buffer(Engine::audioEngine()->framesPerPeriod()),
	m_extOutputEnabled(false),
	m_nextMixerChannel(0),
	m_name(name),
	m_effects(hasEffectChain ? new EffectChain(nullptr) : nullptr),
	m_volumeModel(volumeModel),
	m_panningModel(panningModel),
	m_mutedModel(mutedModel)
{
	m_buffer.allocateInterleavedBuffer();

	Engine::audioEngine()->addAudioBusHandle(this);
	setExtOutputEnabled(true);
}




AudioBusHandle::~AudioBusHandle()
{
	setExtOutputEnabled(false);
	Engine::audioEngine()->removeAudioBusHandle(this);
}




void AudioBusHandle::setExtOutputEnabled(bool enabled)
{
	if (enabled != m_extOutputEnabled)
	{
		m_extOutputEnabled = enabled;
		if (m_extOutputEnabled)
		{
			Engine::audioEngine()->audioDev()->registerPort(this);
		}
		else
		{
			Engine::audioEngine()->audioDev()->unregisterPort(this);
		}
	}
}




void AudioBusHandle::setName(const QString& newName)
{
	m_name = newName;
	Engine::audioEngine()->audioDev()->renamePort(this);
}




bool AudioBusHandle::processEffects()
{
	if (m_effects)
	{
		bool more = m_effects->processAudioBuffer(m_buffer);
		return more;
	}
	return false;
}


f_cnt_t AudioBusHandle::latency() const
{
	return (m_sourceLatency ? m_sourceLatency() : 0) + (m_effects ? m_effects->latency() : 0);
}




void AudioBusHandle::doProcessing()
{
	if (m_mutedModel && m_mutedModel->value())
	{
		// don't let old delayed audio come out when unmuted
		if (m_pdcPending > 0) { m_pdc.clear(); m_pdcPending = 0; }
		return;
	}

	const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();

	// clear the buffer
	m_buffer.silenceAllChannels();

	//qDebug( "Playhandles: %d", m_playHandles.size() );
	for (PlayHandle* ph : m_playHandles) // now we mix all playhandle buffers into our internal buffer
	{
		if (ph->buffer())
		{
			if (ph->usesBuffer()
				&& (ph->type() == PlayHandle::Type::NotePlayHandle
					|| !MixHelpers::isSilent(ph->buffer(), fpp)))
			{
				m_bufferUsage = true;

				// Writing to temporary interleaved buffer until PlayHandle and MixHelpers switch to planar
				MixHelpers::add(m_buffer.interleavedBuffer().asSampleFrames().data(), ph->buffer(), fpp);
			}
			ph->releaseBuffer(); 	// gets rid of playhandle's buffer and sets
									// pointer to null, so if it doesn't get re-acquired we know to skip it next time
		}
	}

	if (m_bufferUsage)
	{
		// PlayHandle buffers were written to the temporary interleaved buffer
		auto buffer = m_buffer.interleavedBuffer();

		// handle volume and panning
		// has both vol and pan models
		if (m_volumeModel && m_panningModel)
		{
			ValueBuffer* volBuf = m_volumeModel->valueBuffer();
			ValueBuffer* panBuf = m_panningModel->valueBuffer();

			// both vol and pan have s.ex.data:
			if (volBuf && panBuf)
			{
				for (f_cnt_t f = 0; f < fpp; ++f)
				{
					float v = volBuf->values()[f] * 0.01f;
					float p = panBuf->values()[f] * 0.01f;
					buffer[f][0] *= (p <= 0 ? 1.0f : 1.0f - p) * v;
					buffer[f][1] *= (p >= 0 ? 1.0f : 1.0f + p) * v;
				}
			}

			// only vol has s.ex.data:
			else if (volBuf)
			{
				float p = m_panningModel->value() * 0.01f;
				float l = (p <= 0 ? 1.0f : 1.0f - p);
				float r = (p >= 0 ? 1.0f : 1.0f + p);
				for (f_cnt_t f = 0; f < fpp; ++f)
				{
					float v = volBuf->values()[f] * 0.01f;
					buffer[f][0] *= v * l;
					buffer[f][1] *= v * r;
				}
			}

			// only pan has s.ex.data:
			else if (panBuf)
			{
				float v = m_volumeModel->value() * 0.01f;
				for (f_cnt_t f = 0; f < fpp; ++f)
				{
					float p = panBuf->values()[f] * 0.01f;
					buffer[f][0] *= (p <= 0 ? 1.0f : 1.0f - p) * v;
					buffer[f][1] *= (p >= 0 ? 1.0f : 1.0f + p) * v;
				}
			}

			// neither has s.ex.data:
			else
			{
				float p = m_panningModel->value() * 0.01f;
				float v = m_volumeModel->value() * 0.01f;
				for (f_cnt_t f = 0; f < fpp; ++f)
				{
					buffer[f][0] *= (p <= 0 ? 1.0f : 1.0f - p) * v;
					buffer[f][1] *= (p >= 0 ? 1.0f : 1.0f + p) * v;
				}
			}
		}

		// has vol model only
		else if (m_volumeModel)
		{
			ValueBuffer* volBuf = m_volumeModel->valueBuffer();

			if (volBuf)
			{
				for (f_cnt_t f = 0; f < fpp; ++f)
				{
					float v = volBuf->values()[f] * 0.01f;
					buffer[f][0] *= v;
					buffer[f][1] *= v;
				}
			}
			else
			{
				float v = m_volumeModel->value() * 0.01f;
				for (f_cnt_t f = 0; f < fpp; ++f)
				{
					buffer[f][0] *= v;
					buffer[f][1] *= v;
				}
			}
		}

		// Copy from temporary interleaved buffer to the main planar buffer
		// so they stay in sync
		toPlanar(buffer, m_buffer.groupBuffers(0));

		const auto sanitized = Engine::audioEngine()->sanitizationEnabled() ? m_buffer.sanitizeAll() : false;
		m_corrupted.store(sanitized, std::memory_order_relaxed);

		// Update silence status of all channels for instrument output
		m_buffer.updateAllSilenceFlags();
	}
	// as of now there's no situation where we only have panning model but no volume model
	// if we have neither, we don't have to do anything here - just pass the audio as is

	// handle effects
	const bool anyOutputAfterEffects = processEffects();
	const bool hasOutput = anyOutputAfterEffects || m_bufferUsage;
	if (hasOutput || m_pdcPending > 0)
	{
		if (m_pdc.delay() > 0)
		{
			// Hold this bus back to line it up with slower inputs of its mixer channel, and keep sending
			// until the delayed signal has come out
			if (!hasOutput) { m_buffer.silenceAllChannels(); }
			m_pdc.process(m_buffer.groupBuffers(0));
			toInterleaved(m_buffer.groupBuffers(0), m_buffer.interleavedBuffer());
			m_buffer.updateAllSilenceFlags();
			m_pdcPending = hasOutput ? m_pdc.delay() : m_pdcPending - std::min(m_pdcPending, fpp);
		}
		else { m_pdcPending = 0; }

		// TODO: improve the flow here - convert to pull model
		Engine::mixer()->captureBounceTap(this, m_buffer);
		Engine::mixer()->mixToChannel(m_buffer, m_nextMixerChannel); // send output to mixer
		m_bufferUsage = false;
	}
}


void AudioBusHandle::addPlayHandle(PlayHandle* handle)
{
	QMutexLocker lockGuard(&m_playHandleLock);
	m_playHandles.append(handle);
}


void AudioBusHandle::removePlayHandle(PlayHandle* handle)
{
	QMutexLocker lockGuard(&m_playHandleLock);
	PlayHandleList::Iterator it = std::find(m_playHandles.begin(), m_playHandles.end(), handle);
	if (it != m_playHandles.end())
	{
		m_playHandles.erase(it);
	}
}

} // namespace lmms
