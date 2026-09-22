/*
 * AudioFileProcessor.cpp - instrument for using audio files
 *
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "AudioFileProcessor.h"
#include "AudioFileProcessorView.h"

#include "InstrumentTrack.h"
#include "PathUtil.h"
#include "Song.h"

#include "LmmsTypes.h"
#include "plugin_export.h"

#include <QDomElement>
#include <QFileInfo>
#include <QRegularExpression>
#include <cmath>
#include <numbers>

#include "AudioEngine.h"
#include "Engine.h"
#include "TimeStretch.h"


namespace lmms
{

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT audiofileprocessor_plugin_descriptor =
{
	LMMS_STRINGIFY( PLUGIN_NAME ),
	"AudioFileProcessor",
	QT_TRANSLATE_NOOP( "PluginBrowser",
				"Simple sampler with various settings for "
				"using samples (e.g. drums) in an "
				"instrument-track" ),
	"Tobias Doerffel <tobydox/at/users.sf.net>",
	0x0100,
	Plugin::Type::Instrument,
	new PluginPixmapLoader( "logo" ),
	"wav,ogg,ds,spx,au,voc,aif,aiff,flac,raw"
#ifdef LMMS_HAVE_SNDFILE_MP3
	",mp3"
#endif
	,
	nullptr,
} ;

}




AudioFileProcessor::AudioFileProcessor( InstrumentTrack * _instrument_track ) :
	Instrument( _instrument_track, &audiofileprocessor_plugin_descriptor ),
	m_ampModel( 100, 0, 500, 1, this, tr( "Amplify" ) ),
	m_startPointModel( 0, 0, 1, 0.0000001f, this, tr( "Start of sample" ) ),
	m_endPointModel( 1, 0, 1, 0.0000001f, this, tr( "End of sample" ) ),
	m_loopPointModel( 0, 0, 1, 0.0000001f, this, tr( "Loopback point" ) ),
	m_reverseModel( false, this, tr( "Reverse sample" ) ),
	m_loopModel( 0, 0, 2, this, tr( "Loop mode" ) ),
	m_stutterModel( false, this, tr( "Stutter" ) ),
	m_interpolationModel( this, tr( "Interpolation mode" ) ),
	m_warpModel(false, this, tr("Warp to song tempo")),
	m_sampleTempoModel(120, 20, 300, this, tr("Sample tempo")),
	m_crossfadeModel(0.f, 0.f, 500.f, 0.1f, this, tr("Loop crossfade")),
	m_nextPlayStartPoint( 0 ),
	m_nextPlayBackwards( false )
{
	connect( &m_reverseModel, SIGNAL( dataChanged() ),
				this, SLOT( reverseModelChanged() ), Qt::DirectConnection );
	connect( &m_ampModel, SIGNAL( dataChanged() ),
				this, SLOT( ampModelChanged() ), Qt::DirectConnection );
	connect( &m_startPointModel, SIGNAL( dataChanged() ),
				this, SLOT( startPointChanged() ), Qt::DirectConnection );
	connect( &m_endPointModel, SIGNAL( dataChanged() ),
				this, SLOT( endPointChanged() ), Qt::DirectConnection );
	connect( &m_loopPointModel, SIGNAL( dataChanged() ),
				this, SLOT( loopPointChanged() ), Qt::DirectConnection );
	connect( &m_stutterModel, SIGNAL( dataChanged() ),
				this, SLOT( stutterModelChanged() ), Qt::DirectConnection );

//interpolation modes
	m_interpolationModel.addItem( tr( "None" ) );
	m_interpolationModel.addItem( tr( "Linear" ) );
	m_interpolationModel.addItem( tr( "Sinc" ) );
	m_interpolationModel.setValue( 1 );

	// Warp and loop crossfade both need a rebuilt playback buffer
	connect(&m_warpModel, &Model::dataChanged, this, &AudioFileProcessor::scheduleRebuild);
	connect(&m_sampleTempoModel, &Model::dataChanged, this, [this] { if (m_warpModel.value()) { scheduleRebuild(); } });
	connect(Engine::getSong(), &Song::tempoChanged, this, [this] { if (m_warpModel.value()) { scheduleRebuild(); } });
	connect(&m_crossfadeModel, &Model::dataChanged, this, &AudioFileProcessor::scheduleRebuild);
	for (Model* model : std::initializer_list<Model*>{&m_loopPointModel, &m_endPointModel, &m_loopModel, &m_reverseModel})
	{
		connect(model, &Model::dataChanged, this, [this] { if (m_crossfadeModel.value() > 0.f) { scheduleRebuild(); } });
	}

	pointChanged();
}




void AudioFileProcessor::playNote( NotePlayHandle * _n,
						SampleFrame* _working_buffer )
{
	const f_cnt_t frames = _n->framesLeftForCurrentPeriod();
	const f_cnt_t offset = _n->noteOffset();

	// Magic key - a frequency < 20 (say, the bottom piano note if using
	// a A4 base tuning) restarts the start point. The note is not actually
	// played.
	if( m_stutterModel.value() == true && _n->frequency() < 20.0 )
	{
		m_nextPlayStartPoint = m_sample.startFrame();
		m_nextPlayBackwards = false;
		return;
	}

	if( !_n->m_pluginData )
	{
		if (m_stutterModel.value() == true && m_nextPlayStartPoint >= static_cast<std::size_t>(m_sample.endFrame()))
		{
			// Restart playing the note if in stutter mode, not in loop mode,
			// and we're at the end of the sample.
			m_nextPlayStartPoint = m_sample.startFrame();
			m_nextPlayBackwards = false;
		}
		// set interpolation mode for libsamplerate
		auto interpolationMode = AudioResampler::Mode::Linear;
		switch( m_interpolationModel.value() )
		{
			case 0:
				interpolationMode = AudioResampler::Mode::ZOH;
				break;
			case 1:
				interpolationMode = AudioResampler::Mode::Linear;
				break;
			case 2:
				interpolationMode = AudioResampler::Mode::SincMedium;
				break;
		}

		_n->m_pluginData = new Sample::PlaybackState(interpolationMode);
		static_cast<Sample::PlaybackState*>(_n->m_pluginData)->setFrameIndex(m_nextPlayStartPoint);
		static_cast<Sample::PlaybackState*>(_n->m_pluginData)->setBackwards(m_nextPlayBackwards);

// debug code
/*		qDebug( "frames %d", m_sample->frames() );
		qDebug( "startframe %d", m_sample->startFrame() );
		qDebug( "nextPlayStartPoint %d", m_nextPlayStartPoint );*/
	}

	if( ! _n->isFinished() )
	{
		if (m_sample.play(_working_buffer + offset,
						static_cast<Sample::PlaybackState*>(_n->m_pluginData),
						frames, static_cast<Sample::Loop>(m_loopModel.value()),
						DefaultBaseFreq / _n->frequency()))
		{
			applyRelease( _working_buffer, _n );
			emit isPlaying(static_cast<Sample::PlaybackState*>(_n->m_pluginData)->frameIndex());
		}
		else
		{
			zeroSampleFrames(_working_buffer, frames + offset);
			emit isPlaying( 0 );
		}
	}
	else
	{
		emit isPlaying( 0 );
	}
	if( m_stutterModel.value() == true )
	{
		m_nextPlayStartPoint = static_cast<Sample::PlaybackState*>(_n->m_pluginData)->frameIndex();
		m_nextPlayBackwards = static_cast<Sample::PlaybackState*>(_n->m_pluginData)->backwards();
	}
}




void AudioFileProcessor::deleteNotePluginData( NotePlayHandle * _n )
{
	delete static_cast<Sample::PlaybackState*>(_n->m_pluginData);
}




void AudioFileProcessor::saveSettings(QDomDocument& doc, QDomElement& elem)
{
	// Always store the original audio; warp and crossfade are re-applied on load
	const auto source = Sample(m_sourceBuffer ? m_sourceBuffer : SampleBuffer::emptyBuffer());
	elem.setAttribute("src", source.sampleFile());
	if (source.sampleFile().isEmpty())
	{
		elem.setAttribute("sampledata", source.toBase64());
	}
	m_reverseModel.saveSettings(doc, elem, "reversed");
	m_loopModel.saveSettings(doc, elem, "looped");
	m_ampModel.saveSettings(doc, elem, "amp");
	m_startPointModel.saveSettings(doc, elem, "sframe");
	m_endPointModel.saveSettings(doc, elem, "eframe");
	m_loopPointModel.saveSettings(doc, elem, "lframe");
	m_stutterModel.saveSettings(doc, elem, "stutter");
	m_interpolationModel.saveSettings(doc, elem, "interp");
	m_warpModel.saveSettings(doc, elem, "warp");
	m_sampleTempoModel.saveSettings(doc, elem, "sampletempo");
	m_crossfadeModel.saveSettings(doc, elem, "xfade");
}




void AudioFileProcessor::loadSettings(const QDomElement& elem)
{
	if (auto srcFile = elem.attribute("src"); !srcFile.isEmpty())
	{
		if (QFileInfo(PathUtil::toAbsolute(srcFile)).exists())
		{
			setAudioFile(srcFile, false);
		}
		else { Engine::getSong()->collectError(QString("%1: %2").arg(tr("Sample not found"), srcFile)); }
	}
	else if (auto sampleData = elem.attribute("sampledata"); !sampleData.isEmpty())
	{
		m_sourceBuffer = SampleBuffer::fromBase64(sampleData);
		m_sample = Sample(m_sourceBuffer);
	}

	m_loopModel.loadSettings(elem, "looped");
	m_ampModel.loadSettings(elem, "amp");
	m_endPointModel.loadSettings(elem, "eframe");
	m_startPointModel.loadSettings(elem, "sframe");

	// compat code for not having a separate loopback point
	if (elem.hasAttribute("lframe") || !elem.firstChildElement("lframe").isNull())
	{
		m_loopPointModel.loadSettings(elem, "lframe");
	}
	else
	{
		m_loopPointModel.loadSettings(elem, "sframe");
	}

	m_reverseModel.loadSettings(elem, "reversed");

	m_stutterModel.loadSettings(elem, "stutter");
	if (elem.hasAttribute("interp") || !elem.firstChildElement("interp").isNull())
	{
		m_interpolationModel.loadSettings(elem, "interp");
	}
	else
	{
		m_interpolationModel.setValue(1.0f); // linear by default
	}

	// Older projects have no warp settings: warp stays off and the detected tempo is kept
	const bool hasTempo = elem.hasAttribute("sampletempo") || !elem.firstChildElement("sampletempo").isNull();
	const int detectedTempo = m_sampleTempoModel.value();
	m_warpModel.loadSettings(elem, "warp");
	m_sampleTempoModel.loadSettings(elem, "sampletempo");
	if (!hasTempo) { m_sampleTempoModel.setValue(detectedTempo); }
	m_crossfadeModel.loadSettings(elem, "xfade");
	m_rebuildQueued = false;
	rebuildPlaybackSample();

	pointChanged();
	emit sampleUpdated();
}




void AudioFileProcessor::loadFile( const QString & _file )
{
	setAudioFile( _file );
}




QString AudioFileProcessor::nodeName() const
{
	return audiofileprocessor_plugin_descriptor.name;
}




auto AudioFileProcessor::beatLen(NotePlayHandle* note) const -> f_cnt_t
{
	// If we can play indefinitely, use the default beat note duration
	if (static_cast<Sample::Loop>(m_loopModel.value()) != Sample::Loop::Off) { return 0; }

	// Otherwise, use the remaining sample duration
	const auto baseFreq = instrumentTrack()->baseFreq();
	const auto freqFactor = baseFreq / note->frequency()
		* Engine::audioEngine()->outputSampleRate()
		/ Engine::audioEngine()->baseSampleRate();
	const auto sampleRateRatio = static_cast<double>(Engine::audioEngine()->outputSampleRate()) / m_sample.sampleRate();

	const auto startFrame = m_nextPlayStartPoint >= static_cast<std::size_t>(m_sample.endFrame())
		? m_sample.startFrame()
		: m_nextPlayStartPoint;
	const auto duration = m_sample.endFrame() - startFrame;

	return static_cast<f_cnt_t>(std::floor(duration * freqFactor * sampleRateRatio));
}




gui::PluginView* AudioFileProcessor::instantiateView( QWidget * _parent )
{
	return new gui::AudioFileProcessorView( this, _parent );
}

void AudioFileProcessor::setAudioFile(const QString& _audio_file, bool _rename)
{
	// is current channel-name equal to previous-filename??
	if( _rename &&
		( instrumentTrack()->name() ==
			QFileInfo(m_sample.sampleFile()).fileName() ||
				m_sample.sampleFile().isEmpty()))
	{
		// then set it to new one
		instrumentTrack()->setName( PathUtil::cleanName( _audio_file ) );
	}
	// else we don't touch the track-name, because the user named it self

	m_sourceBuffer = SampleBuffer::fromFile(_audio_file);
	m_sampleTempoModel.setValue(detectTempo(_audio_file,
		m_sourceBuffer->sampleRate() > 0 ? static_cast<double>(m_sourceBuffer->size()) / m_sourceBuffer->sampleRate() : 0.0));
	m_warpedRatio = 0.0;
	rebuildPlaybackSample();
	loopPointChanged();
	ampModelChanged();
	reverseModelChanged();
	emit sampleUpdated();
}




bool AudioFileProcessor::crossfadeActive() const
{
	// Baked crossfades only make sense for forward loops
	return m_crossfadeModel.value() > 0.f && m_loopModel.value() == static_cast<int>(Sample::Loop::On)
		&& !m_reverseModel.value();
}




void AudioFileProcessor::scheduleRebuild()
{
	if (m_rebuildQueued) { return; }
	m_rebuildQueued = true;
	QMetaObject::invokeMethod(this, [this]
	{
		if (!m_rebuildQueued) { return; } // a direct rebuild already happened
		m_rebuildQueued = false;
		rebuildPlaybackSample();
	}, Qt::QueuedConnection);
}




void AudioFileProcessor::rebuildPlaybackSample()
{
	if (!m_sourceBuffer) { return; }

	const bool warp = m_warpModel.value() && m_sourceBuffer->size() > 0;
	const double ratio = warp ? m_sampleTempoModel.value() / static_cast<double>(Engine::getSong()->getTempo()) : 1.0;
	const bool crossfade = crossfadeActive();

	std::shared_ptr<const SampleBuffer> buffer = m_sourceBuffer;
	if (ratio != 1.0 || crossfade)
	{
		if (ratio != m_warpedRatio)
		{
			// Stretching is the expensive part, so its result is cached per tempo ratio
			m_warpedFrames = ratio == 1.0
				? std::vector<SampleFrame>(m_sourceBuffer->data(), m_sourceBuffer->data() + m_sourceBuffer->size())
				: dsp::timeStretch(m_sourceBuffer->data(), m_sourceBuffer->size(), ratio, m_sourceBuffer->sampleRate());
			m_warpedRatio = ratio;
		}

		auto frames = m_warpedFrames;
		if (crossfade && !frames.empty())
		{
			// Blend the end of the loop into the audio leading up to the loop point, so jumping
			// from the end back to the loop point continues seamlessly
			const auto size = static_cast<long>(frames.size());
			const long loopStart = std::clamp(static_cast<long>(m_loopPointModel.value() * size), 0L, size);
			const long loopEnd = std::clamp(static_cast<long>(m_endPointModel.value() * size), loopStart, size);
			const long length = std::min({static_cast<long>(m_crossfadeModel.value() * 0.001f * m_sourceBuffer->sampleRate()),
				loopStart, loopEnd - loopStart});
			for (long i = 0; i < length; ++i)
			{
				const float x = (i + 0.5f) / length * std::numbers::pi_v<float> / 2.f;
				auto& tail = frames[loopEnd - length + i];
				tail = tail * std::cos(x) + frames[loopStart - length + i] * std::sin(x);
			}
		}
		buffer = std::make_shared<SampleBuffer>(std::move(frames), m_sourceBuffer->sampleRate(), m_sourceBuffer->audioFile());
	}

	Engine::audioEngine()->requestChangeInModel();
	m_sample = Sample(buffer);
	m_sample.setAmplification(m_ampModel.value() / 100.0f);
	m_sample.setReversed(m_reverseModel.value());
	pointChanged();
	Engine::audioEngine()->doneChangeInModel();

	emit sampleUpdated();
}




int AudioFileProcessor::detectTempo(const QString& fileName, double seconds)
{
	static const auto bpmPattern = QRegularExpression(R"((\d{2,3}(?:\.\d+)?)\s*bpm)", QRegularExpression::CaseInsensitiveOption);
	if (const auto match = bpmPattern.match(QFileInfo(fileName).fileName()); match.hasMatch())
	{
		return std::clamp(qRound(match.captured(1).toDouble()), 20, 300);
	}
	if (seconds <= 0.0) { return 120; }

	// Assume the loop is a whole number of 4/4 bars and pick the count giving the most typical tempo
	int best = 120;
	double bestDistance = 1e9;
	for (int bars : {1, 2, 4, 8, 16})
	{
		const double bpm = bars * 4 * 60.0 / seconds;
		if (bpm < 60.0 || bpm > 200.0) { continue; }
		const double distance = std::abs(std::log2(bpm / 120.0));
		if (distance < bestDistance) { bestDistance = distance; best = qRound(bpm); }
	}
	return best;
}




void AudioFileProcessor::reverseModelChanged()
{
	m_sample.setReversed(m_reverseModel.value());
	m_nextPlayStartPoint = m_sample.startFrame();
	m_nextPlayBackwards = false;
	emit sampleUpdated();
}




void AudioFileProcessor::ampModelChanged()
{
	m_sample.setAmplification(m_ampModel.value() / 100.0f);
	emit sampleUpdated();
}


void AudioFileProcessor::stutterModelChanged()
{
	m_nextPlayStartPoint = m_sample.startFrame();
	m_nextPlayBackwards = false;
}


void AudioFileProcessor::startPointChanged()
{
	// check if start is over end and swap values if so
	if( m_startPointModel.value() > m_endPointModel.value() )
	{
		float tmp = m_endPointModel.value();
		m_endPointModel.setValue( m_startPointModel.value() );
		m_startPointModel.setValue( tmp );
	}

	// nudge loop point with end
	if( m_loopPointModel.value() >= m_endPointModel.value() )
	{
		m_loopPointModel.setValue( qMax( m_endPointModel.value() - 0.001f, 0.0f ) );
	}

	// nudge loop point with start
	if( m_loopPointModel.value() < m_startPointModel.value() )
	{
		m_loopPointModel.setValue( m_startPointModel.value() );
	}

	// check if start & end overlap and nudge end up if so
	if( m_startPointModel.value() == m_endPointModel.value() )
	{
		m_endPointModel.setValue( qMin( m_endPointModel.value() + 0.001f, 1.0f ) );
	}

	pointChanged();

}

void AudioFileProcessor::endPointChanged()
{
	// same as start, for now
	startPointChanged();

}

void AudioFileProcessor::loopPointChanged()
{

	// check that loop point is between start-end points and not overlapping with endpoint
	// ...and move start/end points ahead if loop point is moved over them
	if( m_loopPointModel.value() >= m_endPointModel.value() )
	{
		m_endPointModel.setValue( m_loopPointModel.value() + 0.001f );
		if( m_endPointModel.value() == 1.0f )
		{
			m_loopPointModel.setValue( 1.0f - 0.001f );
		}
	}

	// nudge start point with loop
	if( m_loopPointModel.value() < m_startPointModel.value() )
	{
		m_startPointModel.setValue( m_loopPointModel.value() );
	}

	pointChanged();
}

void AudioFileProcessor::pointChanged()
{
	const auto f_start = static_cast<f_cnt_t>(m_startPointModel.value() * m_sample.sampleSize());
	const auto f_end = static_cast<f_cnt_t>(m_endPointModel.value() * m_sample.sampleSize());
	const auto f_loop = static_cast<f_cnt_t>(m_loopPointModel.value() * m_sample.sampleSize());

	m_nextPlayStartPoint = f_start;
	m_nextPlayBackwards = false;

	m_sample.setAllPointFrames(f_start, f_end, f_loop, f_end);
	emit dataChanged();
}


extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin * lmms_plugin_main(Model * model, void *)
{
	return new AudioFileProcessor(static_cast<InstrumentTrack *>(model));
}


}


} // namespace lmms
