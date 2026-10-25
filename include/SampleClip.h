/*
 * SampleClip.h
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

#ifndef LMMS_SAMPLE_CLIP_H
#define LMMS_SAMPLE_CLIP_H

#include <memory>
#include "Clip.h"
#include "Sample.h"

namespace lmms
{

class SampleBuffer;

namespace gui
{

class SampleClipView;

} // namespace gui


class SampleClip : public Clip
{
	Q_OBJECT
	mapPropertyFromModel(bool,isRecord,setRecord,m_recordModel);
public:
	SampleClip(Track* track, Sample sample, bool isPlaying);
	SampleClip(Track* track);
	~SampleClip() override;

	SampleClip& operator=( const SampleClip& that ) = delete;

	void changeLength( const TimePos & _length ) override;
	const QString& sampleFile() const;
	bool hasSampleFileLoaded(const QString & filename) const;

	void saveSettings( QDomDocument & _doc, QDomElement & _parent ) override;
	void loadSettings( const QDomElement & _this ) override;
	inline QString nodeName() const override
	{
		return "sampleclip";
	}

	Sample& sample()
	{
		return m_sample;
	}

	TimePos sampleLength() const;
	void setSampleStartFrame( f_cnt_t startFrame );
	void setSamplePlayLength( f_cnt_t length );
	void setStartTimeOffset(const TimePos& startTimeOffset) override;
	gui::ClipView * createView( gui::TrackView * _tv ) override;

	//! Fade lengths in ticks, measured from the clip's left and right edges
	TimePos fadeIn() const { return m_fadeIn; }
	TimePos fadeOut() const { return m_fadeOut; }
	void setFadeIn(const TimePos& ticks);
	void setFadeOut(const TimePos& ticks);

	//! Clip gain as a linear factor (applied through the sample's amplification)
	float gain() const { return m_sample.amplification(); }
	void setGain(float gain);
	//! Set the gain so the loudest sample of the played region hits @p peak (linear)
	void normalize(float peak = 1.f);

	//! Fade gain at @p pos inside a clip of @p length, all in the same unit.
	//! Overlapping fades are scaled down so they meet instead of overlapping.
	static float fadeGain(float pos, float length, float fadeIn, float fadeOut);

	//! Speed the sample up (or slow it down) by @p factor, changing tempo and pitch together like a turntable.
	//! The clip's length and start offset follow so it keeps covering the same part of the sample.
	void resample(double factor);


	bool isPlaying() const;
	void setIsPlaying(bool isPlaying);
	void setSampleBuffer(std::shared_ptr<const SampleBuffer> sb);

	SampleClip* clone() override
	{
		return new SampleClip(*this);
	}

public slots:
	void setSampleFile(const QString& sf);
	void updateLength();
	void toggleRecord();
	void playbackPositionChanged();
	void updateTrackClips();
	void tempoChanged();

protected:
	SampleClip( const SampleClip& orig );

private:
	Sample m_sample;
	BoolModel m_recordModel;
	bool m_isPlaying;
	int m_startFrameOffset;
	TimePos m_fadeIn = 0;
	TimePos m_fadeOut = 0;
	//! Accumulated resample factor relative to the sample file's own rate, reapplied when the file is loaded
	double m_resampleRatio = 1.0;

	//! Swap in a copy of the sample's buffer that plays at @p sampleRate, keeping reverse and gain
	void setSampleRate(sample_rate_t sampleRate);

	friend class gui::SampleClipView;


signals:
	void sampleChanged();
	void wasReversed();
} ;


} // namespace lmms

#endif // LMMS_SAMPLE_CLIP_H
