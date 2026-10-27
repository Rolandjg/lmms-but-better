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

#include <functional>
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

	//! Speed the sample up (or slow it down) by @p factor, changing tempo and pitch together like a turntable,
	//! or with @p preservePitch, time-stretch it so only the tempo changes.
	//! The clip's length and start offset follow so it keeps covering the same part of the sample.
	//! @p progress, if set, is called with the fraction done while time-stretching.
	void resample(double factor, bool preservePitch = false, const std::function<void(float)>& progress = {});

	//! Called with the fraction done while loadSettings time-stretches a sample, so callers restoring clips
	//! (pasting, dropping) can show progress. Empty for none.
	static void setLoadProgress(std::function<void(float)> callback) { s_loadProgress = std::move(callback); }


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
	//! Accumulated time-stretch (output length / file length), reapplied when the file is loaded
	double m_stretchRatio = 1.0;

	//! When the sample file was last modified and how big it was when it was decoded, so buffers of an
	//! older version of the file are never handed to clips loading the newer one
	struct FileStamp
	{
		qint64 modified = -1;
		qint64 size = -1;
		bool valid() const { return size >= 0; }
		bool operator==(const FileStamp&) const = default;
	};
	FileStamp m_fileStamp;

	static std::function<void(float)> s_loadProgress;

	static FileStamp fileStamp(const QString& file);
	//! A buffer of @p file (as of @p stamp) with the given stretch and resample ratios that some clip still holds,
	//! so loading the same audio again (copying, pasting, duplicating) can share it instead of decoding and stretching
	static std::shared_ptr<const SampleBuffer> cachedBuffer(const QString& file, const FileStamp& stamp,
		double stretchRatio, double resampleRatio);
	//! Offer this clip's buffer to cachedBuffer(); does nothing unless it is exactly the file's audio after
	//! m_stretchRatio and m_resampleRatio
	void cacheBuffer() const;

	//! Swap in a copy of the sample's buffer that plays at @p sampleRate, keeping reverse and gain
	void setSampleRate(sample_rate_t sampleRate);
	//! Swap in @p frames as the sample's buffer at @p sampleRate, keeping reverse and gain
	void setSampleFrames(std::vector<SampleFrame> frames, sample_rate_t sampleRate);
	//! Time-stretch the sample's buffer from the file (or the current buffer if there is no file)
	//! so it ends up @p stretchRatio times as long as the file, keeping its sample rate
	void applyStretch(double stretchRatio, const std::function<void(float)>& progress = {});

	friend class gui::SampleClipView;


signals:
	void sampleChanged();
	void wasReversed();
} ;


} // namespace lmms

#endif // LMMS_SAMPLE_CLIP_H
