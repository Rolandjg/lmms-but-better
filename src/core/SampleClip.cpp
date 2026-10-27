/*
 * SampleClip.cpp
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
 
#include "SampleClip.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <numbers>
#include <tuple>

#include <QDateTime>
#include <QDomElement>
#include <QFileInfo>

#include "PatternStore.h"
#include "PathUtil.h"
#include "SampleClipView.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TimeStretch.h"

namespace lmms
{

std::function<void(float)> SampleClip::s_loadProgress;

namespace
{

struct BufferCacheKey
{
	QString path;
	qint64 modified;
	qint64 size;
	double stretchRatio;
	double resampleRatio;

	bool operator<(const BufferCacheKey& other) const
	{
		return std::tie(path, modified, size, stretchRatio, resampleRatio)
			< std::tie(other.path, other.modified, other.size, other.stretchRatio, other.resampleRatio);
	}
};

//! Weak, so the cache never keeps audio alive on its own: an entry lasts as long as some clip uses the buffer
std::map<BufferCacheKey, std::weak_ptr<const SampleBuffer>>& bufferCache()
{
	static auto cache = std::map<BufferCacheKey, std::weak_ptr<const SampleBuffer>>{};
	return cache;
}

std::mutex& bufferCacheMutex()
{
	static auto mutex = std::mutex{};
	return mutex;
}

} // namespace

SampleClip::SampleClip(Track* _track, Sample sample, bool isPlaying):
	Clip(_track),
	m_sample(std::move(sample)),
	m_isPlaying(false),
	m_startFrameOffset(0)
{
	saveJournallingState( false );
	setSampleFile( "" );
	restoreJournallingState();

	// we need to receive bpm-change-events, because then we have to
	// change length of this Clip
	connect(Engine::getSong(), &Song::tempoChanged, this, &SampleClip::tempoChanged, Qt::DirectConnection);
	connect( Engine::getSong(), SIGNAL(timeSignatureChanged(int,int)),
					this, SLOT(updateLength()));

	//playbutton clicked or space key / on Export Song set isPlaying to false
	connect( Engine::getSong(), SIGNAL(playbackStateChanged()),
			this, SLOT(playbackPositionChanged()), Qt::DirectConnection );
	//care about loops and jumps
	connect(Engine::getSong(), &Song::playbackPositionJumped,
			this, &SampleClip::playbackPositionChanged, Qt::DirectConnection);
	//care about mute Clips
	connect( this, SIGNAL(dataChanged()), this, SLOT(playbackPositionChanged()));
	//care about mute track
	connect( getTrack()->getMutedModel(), SIGNAL(dataChanged()),
			this, SLOT(playbackPositionChanged()), Qt::DirectConnection );
	//care about Clip position
	connect( this, SIGNAL(positionChanged()), this, SLOT(updateTrackClips()));

	updateTrackClips();
}

SampleClip::SampleClip(Track* track)
	: SampleClip(track, Sample(), false)
{
}

SampleClip::SampleClip(const SampleClip& orig) :
	Clip(orig),
	m_sample(std::move(orig.m_sample)),
	m_isPlaying(orig.m_isPlaying),
	m_startFrameOffset(orig.m_startFrameOffset),
	m_fadeIn(orig.m_fadeIn),
	m_fadeOut(orig.m_fadeOut),
	m_resampleRatio(orig.m_resampleRatio),
	m_stretchRatio(orig.m_stretchRatio),
	m_fileStamp(orig.m_fileStamp)
{
	saveJournallingState( false );
	setSampleFile( "" );
	restoreJournallingState();

	// we need to receive bpm-change-events, because then we have to
	// change length of this Clip
	connect(Engine::getSong(), &Song::tempoChanged, this, &SampleClip::tempoChanged, Qt::DirectConnection);
	connect( Engine::getSong(), SIGNAL(timeSignatureChanged(int,int)),
					this, SLOT(updateLength()));

	//playbutton clicked or space key / on Export Song set isPlaying to false
	connect( Engine::getSong(), SIGNAL(playbackStateChanged()),
			this, SLOT(playbackPositionChanged()), Qt::DirectConnection );
	//care about loops and jumps
	connect(Engine::getSong(), &Song::playbackPositionJumped,
			this, &SampleClip::playbackPositionChanged, Qt::DirectConnection);
	//care about mute Clips
	connect( this, SIGNAL(dataChanged()), this, SLOT(playbackPositionChanged()));
	//care about mute track
	connect( getTrack()->getMutedModel(), SIGNAL(dataChanged()),
			this, SLOT(playbackPositionChanged()), Qt::DirectConnection );
	//care about Clip position
	connect( this, SIGNAL(positionChanged()), this, SLOT(updateTrackClips()));

	updateTrackClips();
}




SampleClip::~SampleClip()
{
	auto sampletrack = dynamic_cast<SampleTrack*>(getTrack());
	if ( sampletrack )
	{
		sampletrack->updateClips();
	}
}




void SampleClip::changeLength( const TimePos & _length )
{
	Clip::changeLength(std::max(static_cast<int>(_length), 1));
}



const QString& SampleClip::sampleFile() const
{
	return m_sample.sampleFile();
}

bool SampleClip::hasSampleFileLoaded(const QString & filename) const
{
	return m_sample.sampleFile() == filename;
}

void SampleClip::setSampleBuffer(std::shared_ptr<const SampleBuffer> sb)
{
	{
		const auto guard = Engine::audioEngine()->requestChangesGuard();
		m_sample = Sample(std::move(sb));
	}
	m_resampleRatio = 1.0;
	m_stretchRatio = 1.0;
	m_fileStamp = {};
	updateLength();

	emit sampleChanged();

	Engine::getSong()->setModified();
}

void SampleClip::setSampleFile(const QString& sf)
{
	// Remove any prior offset in the clip
	setStartTimeOffset(0);
	if (!sf.isEmpty())
	{
		const auto stamp = fileStamp(sf);
		auto buffer = cachedBuffer(sf, stamp, 1.0, 1.0);
		if (!buffer) { buffer = SampleBuffer::fromFile(sf); }
		m_sample = Sample(std::move(buffer));
		m_fileStamp = stamp;
		m_resampleRatio = 1.0;
		m_stretchRatio = 1.0;
		cacheBuffer();
		updateLength();
	}
	else
	{
		// If there is no sample, make the clip a bar long
		float nom = Engine::getSong()->getTimeSigModel().getNumerator();
		float den = Engine::getSong()->getTimeSigModel().getDenominator();
		changeLength(DefaultTicksPerBar * (nom / den));
	}

	emit sampleChanged();
	emit playbackPositionChanged();
}




void SampleClip::toggleRecord()
{
	m_recordModel.setValue( !m_recordModel.value() );
	emit dataChanged();
}




void SampleClip::playbackPositionChanged()
{
	Engine::audioEngine()->removePlayHandlesOfTypes( getTrack(), PlayHandle::Type::SamplePlayHandle );
	auto st = dynamic_cast<SampleTrack*>(getTrack());
	st->setPlayingClips( false );
}




void SampleClip::updateTrackClips()
{
	auto sampletrack = dynamic_cast<SampleTrack*>(getTrack());
	if( sampletrack)
	{
		sampletrack->updateClips();
	}
}




bool SampleClip::isPlaying() const
{
	return m_isPlaying;
}




void SampleClip::setIsPlaying(bool isPlaying)
{
	m_isPlaying = isPlaying;
}




void SampleClip::updateLength()
{
	// If the clip has already been manually resized, don't automatically resize it.
	if (getAutoResize())
	{
		if (getTrack()->trackContainer() == Engine::patternStore())
		{
			changeLength(TimePos::ticksPerBar() * Engine::patternStore()->lengthOfPattern(getTrack()->getClipNum(this)));
			return;
		}
		changeLength(sampleLength());
		setStartTimeOffset(0);
	}

	emit sampleChanged();
}


void SampleClip::tempoChanged()
{
	Clip::setStartTimeOffset(std::round(1.0f * m_startFrameOffset / Engine::framesPerTick()));
	updateLength();
	emit sampleChanged();
}

void SampleClip::setStartTimeOffset(const TimePos& startTimeOffset)
{
	m_startFrameOffset = startTimeOffset * Engine::framesPerTick();
	Clip::setStartTimeOffset(startTimeOffset);
}


TimePos SampleClip::sampleLength() const
{
	return static_cast<int>(m_sample.sampleSize() / Engine::framesPerTick(m_sample.sampleRate()));
}




void SampleClip::setSampleStartFrame(f_cnt_t startFrame)
{
	m_sample.setStartFrame(startFrame);
}




void SampleClip::setSamplePlayLength(f_cnt_t length)
{
	m_sample.setEndFrame(length);
}




void SampleClip::saveSettings( QDomDocument & _doc, QDomElement & _this )
{
	if( _this.parentNode().nodeName() == "clipboard" )
	{
		_this.setAttribute( "pos", -1 );
	}
	else
	{
		_this.setAttribute( "pos", startPosition() );
	}
	_this.setAttribute( "len", length() );
	_this.setAttribute( "muted", isMuted() );
	_this.setAttribute( "src", sampleFile() );
	_this.setAttribute( "off", startTimeOffset() );
	_this.setAttribute("autoresize", QString::number(getAutoResize()));
	if( sampleFile() == "" )
	{
		QString s;
		_this.setAttribute("data", m_sample.toBase64());
	}

	_this.setAttribute( "sample_rate", m_sample.sampleRate());
	if (const auto& c = color())
	{
		_this.setAttribute("color", c->name());
	}
	if (m_sample.reversed())
	{
		_this.setAttribute("reversed", "true");
	}
	if (m_fadeIn > 0) { _this.setAttribute("fadein", static_cast<int>(m_fadeIn)); }
	if (m_fadeOut > 0) { _this.setAttribute("fadeout", static_cast<int>(m_fadeOut)); }
	if (gain() != 1.f) { _this.setAttribute("gain", gain()); }
	// Embedded data already stores the resampled rate in sample_rate; only a file has to be resampled again
	// Full precision, so a copy restores exactly the original's ratios and can share its buffer
	if (!sampleFile().isEmpty() && m_resampleRatio != 1.0)
	{
		_this.setAttribute("resample", QString::number(m_resampleRatio, 'g', 17));
	}
	if (!sampleFile().isEmpty() && m_stretchRatio != 1.0)
	{
		_this.setAttribute("stretch", QString::number(m_stretchRatio, 'g', 17));
	}
	// TODO: start- and end-frame
}




void SampleClip::loadSettings( const QDomElement & _this )
{
	if( _this.attribute( "pos" ).toInt() >= 0 )
	{
		movePosition( _this.attribute( "pos" ).toInt() );
	}

	if (const auto srcFile = _this.attribute("src"); !srcFile.isEmpty())
	{
		if (QFileInfo(PathUtil::toAbsolute(srcFile)).exists())
		{
			const auto ratio = [&_this](const QString& name) {
				const double value = _this.attribute(name, "1").toDouble();
				return value > 0. ? value : 1.0;
			};
			const double stretchRatio = ratio("stretch");
			const double resampleRatio = ratio("resample");

			const auto stamp = fileStamp(srcFile);
			if (auto cached = cachedBuffer(srcFile, stamp, stretchRatio, resampleRatio))
			{
				// Another clip (usually the one being copied) already holds exactly this audio, so share it
				setStartTimeOffset(0);
				{
					const auto guard = Engine::audioEngine()->requestChangesGuard();
					m_sample = Sample(std::move(cached));
				}
				m_fileStamp = stamp;
				m_stretchRatio = stretchRatio;
				m_resampleRatio = resampleRatio;
				updateLength();
				playbackPositionChanged();
			}
			else
			{
				setSampleFile(srcFile);
				if (m_sample.sampleSize() > 0)
				{
					if (stretchRatio != 1.0) { applyStretch(stretchRatio, s_loadProgress); }
					if (resampleRatio != 1.0)
					{
						setSampleRate(static_cast<sample_rate_t>(std::lround(m_sample.sampleRate() * resampleRatio)));
						m_resampleRatio = resampleRatio;
					}
					cacheBuffer();
				}
			}
		}
		else { Engine::getSong()->collectError(QString("%1: %2").arg(tr("Sample not found"), srcFile)); }
	}

	if( sampleFile().isEmpty() && _this.hasAttribute( "data" ) )
	{
		auto sampleRate = _this.hasAttribute("sample_rate") ? _this.attribute("sample_rate").toInt() :
			Engine::audioEngine()->outputSampleRate();

		auto buffer = SampleBuffer::fromBase64(_this.attribute("data"), sampleRate);
		m_sample = Sample(std::move(buffer));
	}
	changeLength( _this.attribute( "len" ).toInt() );
	setMuted( _this.attribute( "muted" ).toInt() );
	setStartTimeOffset( _this.attribute( "off" ).toInt() );
	setAutoResize(_this.attribute("autoresize", "1").toInt());

	if (_this.hasAttribute("color"))
	{
		setColor(QColor{_this.attribute("color")});
	}

	if(_this.hasAttribute("reversed"))
	{
		m_sample.setReversed(true);
		emit wasReversed(); // tell SampleClipView to update the view
	}

	m_fadeIn = std::max(0, _this.attribute("fadein", "0").toInt());
	m_fadeOut = std::max(0, _this.attribute("fadeout", "0").toInt());
	m_sample.setAmplification(_this.attribute("gain", "1").toFloat());
	emit sampleChanged();
}




void SampleClip::setFadeIn(const TimePos& ticks)
{
	const auto value = std::clamp(static_cast<int>(ticks), 0, static_cast<int>(length()));
	if (value == m_fadeIn) { return; }
	m_fadeIn = value;
	Engine::getSong()->setModified();
	emit sampleChanged();
}




void SampleClip::setFadeOut(const TimePos& ticks)
{
	const auto value = std::clamp(static_cast<int>(ticks), 0, static_cast<int>(length()));
	if (value == m_fadeOut) { return; }
	m_fadeOut = value;
	Engine::getSong()->setModified();
	emit sampleChanged();
}




void SampleClip::setGain(float gain)
{
	// -inf .. +24 dB
	gain = std::clamp(gain, 0.f, 15.85f);
	if (gain == m_sample.amplification()) { return; }
	m_sample.setAmplification(gain);
	Engine::getSong()->setModified();
	emit sampleChanged();
}




void SampleClip::normalize(float peak)
{
	const auto& buffer = m_sample.buffer();
	if (!buffer || buffer->size() == 0) { return; }

	// Only look at the part of the sample that the clip actually plays
	const auto framesPerTick = Engine::framesPerTick(m_sample.sampleRate());
	const auto size = static_cast<long>(buffer->size());
	auto first = static_cast<long>(-startTimeOffset() * framesPerTick);
	auto last = first + static_cast<long>(length() * framesPerTick);
	if (m_sample.reversed())
	{
		first = size - last;
		last = size - static_cast<long>(-startTimeOffset() * framesPerTick);
	}
	first = std::clamp(first, 0l, size);
	last = std::clamp(last, first, size);

	float max = 0.f;
	for (auto f = first; f < last; ++f)
	{
		const auto& frame = buffer->data()[f];
		max = std::max({max, std::abs(frame.left()), std::abs(frame.right())});
	}
	if (max > 1e-6f) { setGain(peak / max); }
}




float SampleClip::fadeGain(float pos, float length, float fadeIn, float fadeOut)
{
	if (fadeIn + fadeOut > length && length > 0.f)
	{
		const float scale = length / (fadeIn + fadeOut);
		fadeIn *= scale;
		fadeOut *= scale;
	}

	// Equal-power curves: overlapping fades on two tracks cross over without a dip
	constexpr float halfPi = std::numbers::pi_v<float> / 2.f;
	float gain = 1.f;
	if (fadeIn > 0.f && pos < fadeIn)
	{
		gain *= std::sin(std::clamp(pos / fadeIn, 0.f, 1.f) * halfPi);
	}
	const float untilEnd = length - pos;
	if (fadeOut > 0.f && untilEnd < fadeOut)
	{
		gain *= std::sin(std::clamp(untilEnd / fadeOut, 0.f, 1.f) * halfPi);
	}
	return gain;
}




void SampleClip::resample(double factor, bool preservePitch, const std::function<void(float)>& progress)
{
	if (factor <= 0. || factor == 1.0 || m_sample.sampleSize() == 0) { return; }

	double stretch = 1.0;
	if (preservePitch)
	{
		// Keep the sample rate (and so the pitch) and change how many frames the audio spans instead
		const auto oldSize = m_sample.sampleSize();
		applyStretch(m_stretchRatio / factor, progress);
		stretch = static_cast<double>(m_sample.sampleSize()) / oldSize;
	}
	else
	{
		const auto oldRate = m_sample.sampleRate();
		const auto newRate = static_cast<sample_rate_t>(std::max(1l, std::lround(oldRate * factor)));
		if (newRate == oldRate) { return; }

		// A faster sample covers fewer ticks, so scale the clip by the same amount the audio shrinks or grows
		stretch = static_cast<double>(oldRate) / newRate;
		setSampleRate(newRate);
		m_resampleRatio *= static_cast<double>(newRate) / oldRate;
	}

	const auto newLength = static_cast<tick_t>(std::lround(length() * stretch));
	const auto newOffset = static_cast<tick_t>(std::lround(startTimeOffset() * stretch));
	if (getAutoResize()) { updateLength(); }
	else
	{
		changeLength(newLength);
		setStartTimeOffset(newOffset);
	}
	cacheBuffer();
	Engine::getSong()->setModified();
	emit sampleChanged();
}




void SampleClip::applyStretch(double stretchRatio, const std::function<void(float)>& progress)
{
	// Stretch from the file when there is one, so repeated changes don't pile up stretching artifacts
	auto source = m_sample.buffer();
	double ratio = stretchRatio / m_stretchRatio;
	auto sampleRate = m_sample.sampleRate();
	if (m_stretchRatio != 1.0 && !sampleFile().isEmpty())
	{
		const auto stamp = fileStamp(sampleFile());
		auto file = cachedBuffer(sampleFile(), stamp, 1.0, 1.0);
		if (!file) { file = SampleBuffer::fromFile(sampleFile()); }
		if (!file->empty())
		{
			source = std::move(file);
			ratio = stretchRatio;
			// The file may have changed on disk since it was first loaded, rate included
			sampleRate = static_cast<sample_rate_t>(std::max(1l, std::lround(source->sampleRate() * m_resampleRatio)));
			m_fileStamp = stamp;
		}
	}

	setSampleFrames(dsp::timeStretch(source->data(), source->size(), ratio, source->sampleRate(), progress), sampleRate);
	m_stretchRatio = stretchRatio;
}




SampleClip::FileStamp SampleClip::fileStamp(const QString& file)
{
	const auto info = QFileInfo{PathUtil::toAbsolute(file)};
	if (!info.exists()) { return {}; }
	return {info.lastModified().toMSecsSinceEpoch(), info.size()};
}




std::shared_ptr<const SampleBuffer> SampleClip::cachedBuffer(const QString& file, const FileStamp& stamp,
	double stretchRatio, double resampleRatio)
{
	if (file.isEmpty() || !stamp.valid()) { return nullptr; }

	const auto lock = std::lock_guard{bufferCacheMutex()};
	auto& cache = bufferCache();
	const auto it = cache.find({PathUtil::toAbsolute(file), stamp.modified, stamp.size, stretchRatio, resampleRatio});
	if (it == cache.end()) { return nullptr; }
	auto buffer = it->second.lock();
	if (!buffer) { cache.erase(it); }
	return buffer;
}




void SampleClip::cacheBuffer() const
{
	const auto& buffer = m_sample.buffer();
	if (sampleFile().isEmpty() || !m_fileStamp.valid() || !buffer || buffer->empty()) { return; }

	const auto lock = std::lock_guard{bufferCacheMutex()};
	auto& cache = bufferCache();
	std::erase_if(cache, [](const auto& entry) { return entry.second.expired(); });
	cache[{PathUtil::toAbsolute(sampleFile()), m_fileStamp.modified, m_fileStamp.size, m_stretchRatio, m_resampleRatio}]
		= buffer;
}




void SampleClip::setSampleRate(sample_rate_t sampleRate)
{
	const auto& old = *m_sample.buffer();
	setSampleFrames(std::vector<SampleFrame>(old.begin(), old.end()), sampleRate);
}




void SampleClip::setSampleFrames(std::vector<SampleFrame> frames, sample_rate_t sampleRate)
{
	auto sample = Sample(std::make_shared<SampleBuffer>(std::move(frames), sampleRate, m_sample.buffer()->audioFile()));
	sample.setReversed(m_sample.reversed());
	sample.setAmplification(m_sample.amplification());

	const auto guard = Engine::audioEngine()->requestChangesGuard();
	m_sample = std::move(sample);
}




gui::ClipView * SampleClip::createView( gui::TrackView * _tv )
{
	return new gui::SampleClipView( this, _tv );
}


} // namespace lmms
