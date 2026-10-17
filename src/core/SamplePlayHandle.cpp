/*
 * SamplePlayHandle.cpp - implementation of class SamplePlayHandle
 *
 * Copyright (c) 2005-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "SamplePlayHandle.h"

#include <algorithm>

#include "AudioEngine.h"
#include "AudioBusHandle.h"
#include "Engine.h"
#include "PatternTrack.h"
#include "SampleClip.h"
#include "SampleTrack.h"

namespace lmms
{

SamplePlayHandle::SamplePlayHandle(Sample* sample, bool ownAudioBusHandle)
	: PlayHandle(Type::SamplePlayHandle)
	, m_sample(sample)
	, m_ownAudioBusHandle(ownAudioBusHandle)
{
	if (ownAudioBusHandle)
	{
		setAudioBusHandle(new AudioBusHandle("SamplePlayHandle", false));
	}
}




SamplePlayHandle::SamplePlayHandle( const QString& sampleFile ) :
	SamplePlayHandle(new Sample(SampleBuffer::fromFile(sampleFile)), true)
{
}




SamplePlayHandle::SamplePlayHandle( SampleClip* clip ) :
	SamplePlayHandle(&clip->sample(), false)
{
	m_track = clip->getTrack();
	m_clip = clip;
	// SampleTrack::play() has already pointed the sample's start frame at the playback position; turn that
	// back into a position on the clip (the start offset is negative when the clip's left edge was trimmed)
	const auto sampleToOutput = static_cast<double>(Engine::audioEngine()->outputSampleRate()) / m_sample->sampleRate();
	m_clipFrame = static_cast<f_cnt_t>(std::max(0.0,
		m_sample->startFrame() * sampleToOutput + clip->startTimeOffset() * Engine::framesPerTick()));
	setAudioBusHandle(((SampleTrack *)clip->getTrack())->audioBusHandle());
}




SamplePlayHandle::~SamplePlayHandle()
{
	if(m_ownAudioBusHandle)
	{
		delete audioBusHandle();
		delete m_sample;
	}
}




void SamplePlayHandle::play( SampleFrame* buffer )
{
	const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
	//play( 0, _try_parallelizing );
	if( framesDone() >= totalFrames() )
	{
		zeroSampleFrames(buffer, fpp);
		return;
	}

	SampleFrame* workingBuffer = buffer;
	f_cnt_t frames = fpp;

	// apply offset for the first period
	if( framesDone() == 0 )
	{
		zeroSampleFrames(buffer, offset());
		workingBuffer += offset();
		frames -= offset();
	}

	if( !( m_track && m_track->isMuted() )
				&& !(m_patternTrack && m_patternTrack->isMuted()))
	{
/*		StereoVolumeVector v =
			{ { m_volumeModel->value() / DefaultVolume,
				m_volumeModel->value() / DefaultVolume } };*/
		// SamplePlayHandle always plays the sample at its original pitch;
		// it is used only for previews, SampleTracks and the metronome.
		if (!m_sample->play(workingBuffer, &m_state, frames))
		{
			zeroSampleFrames(workingBuffer, frames);
		}
		else if (m_clip && (m_clip->fadeIn() > 0 || m_clip->fadeOut() > 0))
		{
			const auto framesPerTick = static_cast<float>(Engine::framesPerTick());
			const float length = m_clip->length() * framesPerTick;
			const float fadeIn = m_clip->fadeIn() * framesPerTick;
			const float fadeOut = m_clip->fadeOut() * framesPerTick;
			const auto start = m_clipFrame + m_frame;
			for (f_cnt_t f = 0; f < frames; ++f)
			{
				workingBuffer[f] *= SampleClip::fadeGain(static_cast<float>(start + f), length, fadeIn, fadeOut);
			}
		}
	}

	m_frame += frames;
}




bool SamplePlayHandle::isFinished() const
{
	return framesDone() >= totalFrames() && m_doneMayReturnTrue == true;
}




bool SamplePlayHandle::isFromTrack( const Track * _track ) const
{
	return m_track == _track || m_patternTrack == _track;
}




f_cnt_t SamplePlayHandle::totalFrames() const
{
	return (m_sample->endFrame() - m_sample->startFrame()) *
			(static_cast<float>(Engine::audioEngine()->outputSampleRate()) / m_sample->sampleRate());
}


} // namespace lmms
