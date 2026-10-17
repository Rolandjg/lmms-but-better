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

#include <QCollator>
#include <QDir>
#include <QDomElement>
#include <QFileInfo>
#include <QRegularExpression>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>

#include "AudioEngine.h"
#include "Clipboard.h"
#include "DataFile.h"
#include "Engine.h"
#include "Note.h"
#include "NotePlayHandle.h"
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


namespace
{

// Short fades that keep slices and choked notes from clicking
constexpr float SliceFadeInMs = 0.3f;
constexpr float SliceFadeOutMs = 3.f;
constexpr float ChokeFadeMs = 4.f;
constexpr float ReleaseMs = 3.f;
// One-shot notes keep playing after note-off until the sample ends, up to this long
constexpr float MaxOneShotMs = 60000.f;
constexpr int MaxSlices = 128;

//! Equal-power fade gain at @p pos frames into a region of @p length frames
float fadeGain(double pos, double length, float fadeIn, float fadeOut)
{
	constexpr float halfPi = std::numbers::pi_v<float> / 2.f;
	float gain = 1.f;
	if (fadeIn > 0.f && pos < fadeIn) { gain *= std::sin(std::clamp(static_cast<float>(pos) / fadeIn, 0.f, 1.f) * halfPi); }
	const auto untilEnd = static_cast<float>(length - pos);
	if (fadeOut > 0.f && untilEnd < fadeOut) { gain *= std::sin(std::clamp(untilEnd / fadeOut, 0.f, 1.f) * halfPi); }
	return gain;
}

} // namespace


struct AudioFileProcessor::NoteState
{
	explicit NoteState(AudioResampler::Mode mode) : playback(mode) {}

	Sample::PlaybackState playback;
	//! A copy of the sample bounded to this note's slice (slice mode only)
	std::optional<Sample> slice;
	bool silent = false;
	std::uint64_t id = 0;
	//! Logical frame the output has reached, tracked from the playback rate so fades are
	//! sample-accurate even though the resampler reads ahead
	double position = 0.0;
	int regionStart = 0;
	int regionEnd = 0;
	float fadeIn = 0.f;
	float fadeOut = 0.f;
	int chokeFramesDone = -1;
};




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
	m_modeModel(static_cast<int>(Mode::Classic), 0, 1, this, tr("Playback mode")),
	m_sliceMethodModel(static_cast<int>(SliceMethod::Transients), 0, 2, this, tr("Slice by")),
	m_sliceSensitivityModel(50.f, 0.f, 100.f, 1.f, this, tr("Slice sensitivity")),
	m_sliceDivisionModel(1, 0, 4, this, tr("Slice division")),
	m_oneShotModel(false, this, tr("One-shot")),
	m_chokeModel(false, this, tr("Choke")),
	m_snapModel(true, this, tr("Snap to zero crossings")),
	m_fadeInModel(0.f, 0.f, 5000.f, 0.1f, this, tr("Fade in")),
	m_fadeOutModel(0.f, 0.f, 5000.f, 0.1f, this, tr("Fade out")),
	m_transposeModel(0.f, -48.f, 48.f, 1.f, this, tr("Transpose")),
	m_tuneModel(0.f, -100.f, 100.f, 1.f, this, tr("Fine tune")),
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

	// Fades are mostly short, so give the knobs more travel there
	m_fadeInModel.setScaleLogarithmic(true);
	m_fadeOutModel.setScaleLogarithmic(true);

	// Automatic slicing follows its parameters; the beat grid is anchored at the start point
	for (Model* model : std::initializer_list<Model*>{&m_sliceMethodModel, &m_sliceSensitivityModel,
		&m_sliceDivisionModel, &m_sampleTempoModel, &m_startPointModel, &m_endPointModel})
	{
		connect(model, &Model::dataChanged, this, &AudioFileProcessor::scheduleSlicing);
	}

	pointChanged();
}




void AudioFileProcessor::playNote( NotePlayHandle * _n,
						SampleFrame* _working_buffer )
{
	const f_cnt_t frames = _n->framesLeftForCurrentPeriod();
	const f_cnt_t offset = _n->noteOffset();
	const bool slicing = sliceMode();

	// Magic key - a frequency < 20 (say, the bottom piano note if using
	// a A4 base tuning) restarts the start point. The note is not actually
	// played.
	if (!slicing && m_stutterModel.value() == true && _n->frequency() < 20.0)
	{
		m_nextPlayStartPoint = m_sample.startFrame();
		m_nextPlayBackwards = false;
		return;
	}

	if( !_n->m_pluginData )
	{
		const bool stutter = !slicing && m_stutterModel.value();
		if (stutter && m_nextPlayStartPoint >= static_cast<std::size_t>(m_sample.endFrame()))
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

		auto state = new NoteState(interpolationMode);
		_n->m_pluginData = state;
		state->id = ++m_noteCounter;
		const float framesPerMs = m_sample.sampleRate() / 1000.f;

		if (slicing)
		{
			// Each key from the base note upwards plays the next slice
			const auto bounds = sliceBoundaries();
			const int index = _n->key() - instrumentTrack()->baseNote();
			if (index < 0 || index + 1 >= static_cast<int>(bounds.size())) { state->silent = true; }
			else
			{
				state->regionStart = bounds[index];
				state->regionEnd = bounds[index + 1];
				state->slice.emplace(m_sample);
				state->slice->setAllPointFrames(state->regionStart, state->regionEnd, state->regionStart, state->regionEnd);
				state->fadeIn = SliceFadeInMs * framesPerMs;
				state->fadeOut = SliceFadeOutMs * framesPerMs;
				state->playback.setFrameIndex(state->regionStart);
			}
		}
		else
		{
			state->regionStart = m_sample.startFrame();
			state->regionEnd = m_sample.endFrame();
			state->fadeIn = m_fadeInModel.value() * framesPerMs;
			state->fadeOut = m_fadeOutModel.value() * framesPerMs;
			state->playback.setFrameIndex(m_nextPlayStartPoint);
			state->playback.setBackwards(m_nextPlayBackwards);
		}
		state->position = state->playback.frameIndex();
	}

	auto state = static_cast<NoteState*>(_n->m_pluginData);
	const Sample& sample = state->slice ? *state->slice : m_sample;
	const auto loop = static_cast<Sample::Loop>(m_loopModel.value());

	// Transpose and fine tune act on top of the played key. Slices keep their original pitch.
	const double tuning = std::exp2(m_transposeModel.value() / 12.0 + m_tuneModel.value() / 1200.0);
	const double ratio = slicing ? 1.0 / tuning : DefaultBaseFreq / (_n->frequency() * tuning);

	if (!_n->isFinished() && !state->silent
		&& sample.play(_working_buffer + offset, &state->playback, frames, loop, ratio))
	{
		SampleFrame* buf = _working_buffer + offset;

		// Fades follow the output position; the fade-out only applies when the region plays once
		const double step = static_cast<double>(sample.sampleRate())
			/ Engine::audioEngine()->outputSampleRate() / ratio;
		const float fadeOut = loop == Sample::Loop::Off ? state->fadeOut : 0.f;
		if (state->fadeIn > 0.f || fadeOut > 0.f)
		{
			const double length = state->regionEnd - state->regionStart;
			for (f_cnt_t f = 0; f < frames; ++f)
			{
				buf[f] *= fadeGain(state->position + f * step - state->regionStart, length, state->fadeIn, fadeOut);
			}
		}
		state->position += frames * step;

		// Choke: a newer note of this instrument fades out the older ones
		if (m_chokeModel.value() && state->id != m_noteCounter.load(std::memory_order_relaxed))
		{
			if (state->chokeFramesDone < 0) { state->chokeFramesDone = 0; }
			const auto chokeFrames = static_cast<int>(ChokeFadeMs * Engine::audioEngine()->outputSampleRate() / 1000.f);
			for (f_cnt_t f = 0; f < frames; ++f, ++state->chokeFramesDone)
			{
				buf[f] *= std::max(0.f, 1.f - static_cast<float>(state->chokeFramesDone) / chokeFrames);
			}
			if (state->chokeFramesDone >= chokeFrames) { state->silent = true; }
		}

		if (oneShotActive())
		{
			// One-shot notes ignore note-off; just avoid a click if the extended release runs out
			const auto releaseFrames = static_cast<f_cnt_t>(ReleaseMs * Engine::audioEngine()->outputSampleRate() / 1000.f);
			const auto endFrame = _n->framesLeft();
			for (auto f = endFrame - std::min(endFrame, releaseFrames); f < endFrame && f < frames + offset; ++f)
			{
				_working_buffer[f] *= static_cast<float>(endFrame - f) / releaseFrames;
			}
		}
		else { applyRelease(_working_buffer, _n); }

		m_playFrame.store(state->playback.frameIndex(), std::memory_order_relaxed);
		m_playKey.store(_n->key(), std::memory_order_relaxed);
		m_playTick.fetch_add(1, std::memory_order_relaxed);
	}
	else
	{
		zeroSampleFrames(_working_buffer, frames + offset);
	}

	if (m_stutterModel.value() && !slicing)
	{
		m_nextPlayStartPoint = state->playback.frameIndex();
		m_nextPlayBackwards = state->playback.backwards();
	}
}




void AudioFileProcessor::deleteNotePluginData( NotePlayHandle * _n )
{
	delete static_cast<NoteState*>(_n->m_pluginData);
}




float AudioFileProcessor::desiredReleaseTimeMs() const
{
	if (!oneShotActive()) { return ReleaseMs; }

	// Long enough for the region to finish even when played two octaves down
	const auto regionFrames = m_sample.endFrame() - m_sample.startFrame();
	const float ms = regionFrames * 1000.f / std::max(1, m_sample.sampleRate());
	return std::clamp(ms * 4.f, ReleaseMs, MaxOneShotMs);
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
	m_modeModel.saveSettings(doc, elem, "mode");
	m_sliceMethodModel.saveSettings(doc, elem, "slicemethod");
	m_sliceSensitivityModel.saveSettings(doc, elem, "slicesens");
	m_sliceDivisionModel.saveSettings(doc, elem, "slicediv");
	m_oneShotModel.saveSettings(doc, elem, "oneshot");
	m_chokeModel.saveSettings(doc, elem, "choke");
	m_snapModel.saveSettings(doc, elem, "snap");
	m_fadeInModel.saveSettings(doc, elem, "fadein");
	m_fadeOutModel.saveSettings(doc, elem, "fadeout");
	m_transposeModel.saveSettings(doc, elem, "transpose");
	m_tuneModel.saveSettings(doc, elem, "tune");

	QStringList slices;
	for (float slice : m_slices) { slices << QString::number(slice, 'g', 9); }
	elem.setAttribute("slices", slices.join(','));
}




void AudioFileProcessor::loadSettings(const QDomElement& elem)
{
	// Loading sets the points of an already reversed sample; don't flip them again
	m_loading = true;

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

	// Settings added later default to the old behavior when they are missing
	const auto load = [&elem](AutomatableModel& model, const QString& name, float fallback)
	{
		if (elem.hasAttribute(name) || !elem.firstChildElement(name).isNull()) { model.loadSettings(elem, name); }
		else { model.setValue(fallback); }
	};
	load(m_modeModel, "mode", static_cast<int>(Mode::Classic));
	load(m_sliceMethodModel, "slicemethod", static_cast<int>(SliceMethod::Transients));
	load(m_sliceSensitivityModel, "slicesens", 50.f);
	load(m_sliceDivisionModel, "slicediv", 1);
	load(m_oneShotModel, "oneshot", 0);
	load(m_chokeModel, "choke", 0);
	load(m_snapModel, "snap", 1);
	load(m_fadeInModel, "fadein", 0.f);
	load(m_fadeOutModel, "fadeout", 0.f);
	load(m_transposeModel, "transpose", 0.f);
	load(m_tuneModel, "tune", 0.f);
	// Loading resets the scale type
	m_fadeInModel.setScaleLogarithmic(true);
	m_fadeOutModel.setScaleLogarithmic(true);

	m_rebuildQueued = false;
	rebuildPlaybackSample();

	// Automatic slices are recomputed if they were saved before there was anything to slice
	const bool keepSlices = elem.hasAttribute("slices")
		&& (!elem.attribute("slices").isEmpty() || m_sliceMethodModel.value() == static_cast<int>(SliceMethod::Manual));
	if (keepSlices)
	{
		std::vector<float> slices;
		for (const auto& value : elem.attribute("slices").split(',', Qt::SkipEmptyParts))
		{
			slices.push_back(std::clamp(value.toFloat(), 0.f, 1.f));
		}
		std::sort(slices.begin(), slices.end());
		Engine::audioEngine()->requestChangeInModel();
		m_slices = std::move(slices);
		Engine::audioEngine()->doneChangeInModel();
		m_slicingQueued = false;
		emit slicesChanged();
	}
	else
	{
		m_slicingQueued = false;
		updateSlices();
	}
	m_loading = false;

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

	const auto sampleRateRatio = static_cast<double>(Engine::audioEngine()->outputSampleRate()) / m_sample.sampleRate();
	if (sliceMode())
	{
		const auto bounds = sliceBoundaries();
		const int index = note->key() - instrumentTrack()->baseNote();
		if (index < 0 || index + 1 >= static_cast<int>(bounds.size())) { return 0; }
		return static_cast<f_cnt_t>((bounds[index + 1] - bounds[index]) * sampleRateRatio);
	}

	// Otherwise, use the remaining sample duration
	const auto baseFreq = instrumentTrack()->baseFreq();
	const auto freqFactor = baseFreq / note->frequency()
		* Engine::audioEngine()->outputSampleRate()
		/ Engine::audioEngine()->baseSampleRate();

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
	m_nextPlayStartPoint = m_sample.startFrame();
	m_nextPlayBackwards = false;

	// Hand-placed slices belong to the old sample
	if (!m_loading)
	{
		if (m_sliceMethodModel.value() == static_cast<int>(SliceMethod::Manual))
		{
			m_sliceMethodModel.setValue(static_cast<int>(SliceMethod::Transients));
		}
		m_slicingQueued = false;
		updateSlices();
	}
	emit sampleUpdated();
}




bool AudioFileProcessor::crossfadeActive() const
{
	// Baked crossfades only make sense for forward loops
	return m_crossfadeModel.value() > 0.f && m_loopModel.value() == static_cast<int>(Sample::Loop::On)
		&& !m_reverseModel.value() && !sliceMode();
}




bool AudioFileProcessor::oneShotActive() const
{
	// A looping note has no natural end, so it stays gated by the note length
	return m_oneShotModel.value() && m_loopModel.value() == static_cast<int>(Sample::Loop::Off);
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




void AudioFileProcessor::scheduleSlicing()
{
	if (m_loading || m_slicingQueued) { return; }
	m_slicingQueued = true;
	QMetaObject::invokeMethod(this, [this]
	{
		if (!m_slicingQueued) { return; }
		m_slicingQueued = false;
		updateSlices();
	}, Qt::QueuedConnection);
}




void AudioFileProcessor::updateSlices()
{
	const auto method = static_cast<SliceMethod>(m_sliceMethodModel.value());
	// Manual slices are only changed by hand (switching to manual keeps the current ones to edit)
	if (method == SliceMethod::Manual) { return; }

	const auto size = static_cast<int>(m_sample.sampleSize());
	std::vector<float> slices;
	if (size > 1 && method == SliceMethod::Transients)
	{
		for (int frame : detectTransients(m_sample, m_sliceSensitivityModel.value() / 100.f))
		{
			slices.push_back(static_cast<float>(frame) / size);
		}
	}
	else if (size > 1 && method == SliceMethod::Beats)
	{
		// Divisions: 1/16, 1/8, 1/4, 1/2, 1 bar of the sample's own tempo, from the start point.
		// The playback sample may be warped, so measure in the source's length.
		static constexpr auto beatsPerSlice = std::array{0.25, 0.5, 1.0, 2.0, 4.0};
		const auto sourceSeconds = m_sourceBuffer && m_sourceBuffer->sampleRate() > 0
			? static_cast<double>(m_sourceBuffer->size()) / m_sourceBuffer->sampleRate() : 0.0;
		if (sourceSeconds > 0.0)
		{
			const double sliceSeconds = 60.0 / m_sampleTempoModel.value()
				* beatsPerSlice[std::clamp(m_sliceDivisionModel.value(), 0, 4)];
			const double step = sliceSeconds / sourceSeconds;
			for (double pos = m_startPointModel.value() + step;
				pos < m_endPointModel.value() - step * 0.25 && static_cast<int>(slices.size()) < MaxSlices; pos += step)
			{
				slices.push_back(static_cast<float>(pos));
			}
		}
	}

	Engine::audioEngine()->requestChangeInModel();
	m_slices = std::move(slices);
	Engine::audioEngine()->doneChangeInModel();
	emit slicesChanged();
}




void AudioFileProcessor::setSlices(std::vector<float> slices)
{
	for (auto& slice : slices) { slice = std::clamp(slice, 0.f, 1.f); }
	std::sort(slices.begin(), slices.end());
	if (static_cast<int>(slices.size()) > MaxSlices) { slices.resize(MaxSlices); }

	if (m_sliceMethodModel.value() != static_cast<int>(SliceMethod::Manual))
	{
		m_sliceMethodModel.setValue(static_cast<int>(SliceMethod::Manual));
		m_slicingQueued = false;
	}
	Engine::audioEngine()->requestChangeInModel();
	m_slices = std::move(slices);
	Engine::audioEngine()->doneChangeInModel();
	emit slicesChanged();
	Engine::getSong()->setModified();
}




std::vector<int> AudioFileProcessor::sliceBoundaries() const
{
	const int start = m_sample.startFrame();
	const int end = m_sample.endFrame();
	const auto size = static_cast<double>(m_sample.sampleSize());

	std::vector<int> bounds{start};
	for (float slice : m_slices)
	{
		const auto frame = static_cast<int>(slice * size);
		// Ignore markers outside the region and slivers shorter than a millisecond
		if (frame > bounds.back() + m_sample.sampleRate() / 1000 && frame < end - m_sample.sampleRate() / 1000)
		{
			bounds.push_back(frame);
		}
	}
	bounds.push_back(end);
	return bounds;
}




void AudioFileProcessor::copySlicesAsNotes() const
{
	const auto bounds = sliceBoundaries();
	if (bounds.size() < 2 || m_sample.sampleRate() <= 0) { return; }

	using namespace Clipboard;
	DataFile dataFile(DataFile::Type::ClipboardData);
	QDomElement noteList = dataFile.createElement("note-list");
	dataFile.content().appendChild(noteList);

	// Lay the slices out where they occur when the sample plays at its natural speed
	// (the warped length when warp is on)
	const double framesPerTick = Engine::framesPerTick();
	const double toOutput = static_cast<double>(Engine::audioEngine()->outputSampleRate()) / m_sample.sampleRate();
	for (std::size_t i = 0; i + 1 < bounds.size(); ++i)
	{
		const auto begin = static_cast<int>(std::round((bounds[i] - bounds.front()) * toOutput / framesPerTick));
		const auto end = static_cast<int>(std::round((bounds[i + 1] - bounds.front()) * toOutput / framesPerTick));
		Note note(TimePos(std::max(1, end - begin)), TimePos(begin), instrumentTrack()->baseNote() + static_cast<int>(i));
		note.saveState(dataFile, noteList);
	}
	copyString(dataFile.toString(), MimeType::Default);
}




float AudioFileProcessor::monoAt(int frame) const
{
	const auto size = static_cast<int>(m_sample.sampleSize());
	const auto& value = m_sample.data()[m_sample.reversed() ? size - frame - 1 : frame];
	return value.left() + value.right();
}




int AudioFileProcessor::nearestZeroCrossing(int frame) const
{
	const auto size = static_cast<int>(m_sample.sampleSize());
	if (size < 2) { return frame; }
	frame = std::clamp(frame, 0, size - 1);

	// Look up to 5 ms either side for the closest sign change
	const int radius = std::max(1, m_sample.sampleRate() / 200);
	for (int d = 0; d <= radius; ++d)
	{
		for (int candidate : {frame - d, frame + d})
		{
			if (candidate <= 0 || candidate >= size) { continue; }
			const float a = monoAt(candidate - 1);
			const float b = monoAt(candidate);
			if ((a <= 0.f && b > 0.f) || (a >= 0.f && b < 0.f) || b == 0.f) { return candidate; }
		}
	}
	return frame;
}




void AudioFileProcessor::normalize()
{
	const int start = m_sample.startFrame();
	const int end = std::min<int>(m_sample.endFrame(), m_sample.sampleSize());
	float peak = 0.f;
	for (int i = start; i < end; ++i)
	{
		const auto& frame = m_sample.data()[i];
		peak = std::max({peak, std::abs(frame.left()), std::abs(frame.right())});
	}
	if (peak <= 0.f) { return; }
	m_ampModel.setValue(std::min(m_ampModel.maxValue(), 100.f / peak));
}




float AudioFileProcessor::detectRootKey()
{
	if (!m_sourceBuffer || m_sourceBuffer->size() < 4096) { return 0.f; }

	// Analyse the original audio between the start and end points
	const auto size = static_cast<int>(m_sourceBuffer->size());
	const auto toSource = [&](float pos)
	{
		const auto frame = static_cast<int>(pos * size);
		return m_reverseModel.value() ? size - frame : frame;
	};
	const int a = std::clamp(std::min(toSource(m_startPointModel.value()), toSource(m_endPointModel.value())), 0, size);
	const int b = std::clamp(std::max(toSource(m_startPointModel.value()), toSource(m_endPointModel.value())), 0, size);
	std::vector<float> mono(b - a);
	for (int i = a; i < b; ++i)
	{
		mono[i - a] = (m_sourceBuffer->data()[i].left() + m_sourceBuffer->data()[i].right()) * 0.5f;
	}

	const float frequency = detectPitch(mono.data(), static_cast<int>(mono.size()), m_sourceBuffer->sampleRate());
	if (frequency <= 0.f) { return 0.f; }

	// Make the detected pitch the base note (which plays the sample unchanged) and correct the
	// remaining cents with the fine tune
	const double key = 69.0 + 12.0 * std::log2(frequency / 440.0);
	const int rounded = static_cast<int>(std::lround(key));
	instrumentTrack()->baseNoteModel()->setValue(std::clamp(rounded, 0, NumKeys - 1));
	m_tuneModel.setValue(static_cast<float>(std::round((rounded - key) * 100.0)));
	m_transposeModel.setValue(0.f);
	Engine::getSong()->setModified();
	return frequency;
}




void AudioFileProcessor::stepSampleFile(int direction)
{
	const auto current = QFileInfo(PathUtil::toAbsolute(m_sample.sampleFile()));
	if (!current.exists()) { return; }

	QStringList filters;
	for (const auto& ext : QString(audiofileprocessor_plugin_descriptor.supportedFileTypes).split(','))
	{
		filters << "*." + ext;
	}
	auto files = current.dir().entryList(filters, QDir::Files | QDir::Readable);
	// Case-insensitive, numbers in natural order ("kick 2" before "kick 10")
	QCollator collator;
	collator.setNumericMode(true);
	collator.setCaseSensitivity(Qt::CaseInsensitive);
	std::sort(files.begin(), files.end(), collator);
	if (files.size() < 2) { return; }

	auto index = files.indexOf(current.fileName());
	index = index < 0 ? 0 : (index + direction + files.size()) % files.size();
	setAudioFile(current.dir().absoluteFilePath(files[index]));
	Engine::getSong()->setModified();
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




std::vector<int> AudioFileProcessor::detectTransients(const Sample& sample, float sensitivity)
{
	const auto size = static_cast<int>(sample.sampleSize());
	const int sampleRate = std::max(1, sample.sampleRate());
	const bool reversed = sample.reversed();
	const auto mono = [&](int i)
	{
		const auto& frame = sample.data()[reversed ? size - i - 1 : i];
		return (frame.left() + frame.right()) * 0.5f;
	};

	// Energy per ~6 ms hop, full band and with a first-difference high boost so both thumps and
	// clicks register
	const int hop = std::max(64, sampleRate / 172);
	const int hops = size / hop;
	if (hops < 4) { return {}; }
	std::vector<float> level(hops);
	float previous = 0.f;
	float loudest = -200.f;
	for (int h = 0; h < hops; ++h)
	{
		double full = 0.0, high = 0.0;
		for (int i = h * hop; i < (h + 1) * hop; ++i)
		{
			const float x = mono(i);
			full += x * x;
			high += (x - previous) * (x - previous);
			previous = x;
		}
		level[h] = 0.5f * (10.f * std::log10(static_cast<float>(full / hop) + 1e-12f)
			+ 10.f * std::log10(static_cast<float>(high / hop) * 4.f + 1e-12f));
		loudest = std::max(loudest, level[h]);
	}

	// Onset strength: how far a hop rises above the recent past
	std::vector<float> onset(hops, 0.f);
	for (int h = 1; h < hops; ++h)
	{
		float past = 0.f;
		int count = 0;
		for (int k = std::max(0, h - 4); k < h; ++k) { past += level[k]; ++count; }
		onset[h] = std::max(0.f, level[h] - std::max(past / count, level[h - 1] - 1.f));
	}

	const float threshold = 3.f + (1.f - sensitivity) * 12.f; // dB
	const float floor = loudest - 30.f - sensitivity * 20.f;
	const int minGap = static_cast<int>(sampleRate * (0.12f - 0.08f * sensitivity)) / hop;

	std::vector<int> result;
	int last = -minGap - 1;
	for (int h = 1; h < hops - 1 && static_cast<int>(result.size()) < MaxSlices; ++h)
	{
		if (onset[h] < threshold || level[h] < floor || h - last < minGap) { continue; }
		if (onset[h] < onset[h - 1] || onset[h] < onset[h + 1]) { continue; }

		// Refine inside the hop before and this one: the first 32-frame window that gets loud
		const int from = std::max(0, (h - 1) * hop);
		const int to = std::min(size, (h + 1) * hop);
		constexpr int window = 32;
		float maxEnergy = 0.f;
		std::vector<float> energies;
		for (int i = from; i + window <= to; i += window)
		{
			float e = 0.f;
			for (int j = i; j < i + window; ++j) { e += mono(j) * mono(j); }
			energies.push_back(e);
			maxEnergy = std::max(maxEnergy, e);
		}
		int frame = h * hop;
		for (std::size_t w = 0; w < energies.size(); ++w)
		{
			if (energies[w] > maxEnergy * 0.1f) { frame = from + static_cast<int>(w) * window; break; }
		}
		// A little pre-roll, then back up to a zero crossing so the slice starts cleanly
		frame = std::max(0, frame - sampleRate / 1000);
		for (int i = frame, limit = std::max(1, frame - sampleRate / 500); i >= limit; --i)
		{
			if ((mono(i - 1) <= 0.f) != (mono(i) <= 0.f)) { frame = i; break; }
		}
		// The region start is always a slice boundary
		if (frame < sampleRate / 50) { last = h; continue; }

		result.push_back(frame);
		last = h;
	}
	return result;
}




float AudioFileProcessor::detectPitch(const float* data, int size, int sampleRate)
{
	// YIN on a few windows after the attack; the median of the confident estimates wins
	constexpr int window = 2048;
	const int maxLag = std::min(window, sampleRate / 40);
	const int minLag = std::max(2, sampleRate / 2000);
	if (size < window + maxLag) { return 0.f; }

	const int skip = std::min(size - window - maxLag, sampleRate / 20);
	const int span = size - window - maxLag - skip;
	std::vector<float> estimates;
	std::vector<float> diff(maxLag + 1);
	for (int n = 0; n < 7; ++n)
	{
		const float* x = data + skip + span * n / 7;

		// Skip quiet windows
		double energy = 0.0;
		for (int i = 0; i < window; ++i) { energy += x[i] * x[i]; }
		if (energy / window < 1e-6) { continue; }

		diff[0] = 1.f;
		double running = 0.0;
		for (int lag = 1; lag <= maxLag; ++lag)
		{
			double d = 0.0;
			for (int i = 0; i < window; ++i) { const float delta = x[i] - x[i + lag]; d += delta * delta; }
			running += d;
			diff[lag] = running > 0.0 ? static_cast<float>(d * lag / running) : 1.f;
		}

		// The first dip below the threshold, followed to its minimum
		int best = -1;
		for (int lag = minLag; lag < maxLag; ++lag)
		{
			if (diff[lag] < 0.15f)
			{
				while (lag + 1 < maxLag && diff[lag + 1] < diff[lag]) { ++lag; }
				best = lag;
				break;
			}
		}
		if (best < 0) { continue; }

		// Parabolic interpolation around the minimum
		const float a = diff[best - 1], b = diff[best], c = diff[best + 1];
		const float denominator = a - 2.f * b + c;
		const float shift = std::abs(denominator) > 1e-9f ? 0.5f * (a - c) / denominator : 0.f;
		estimates.push_back(sampleRate / (best + std::clamp(shift, -1.f, 1.f)));
	}
	if (estimates.size() < 2) { return 0.f; }
	std::sort(estimates.begin(), estimates.end());
	return estimates[estimates.size() / 2];
}




void AudioFileProcessor::reverseModelChanged()
{
	m_sample.setReversed(m_reverseModel.value());
	m_nextPlayStartPoint = m_sample.startFrame();
	m_nextPlayBackwards = false;

	if (!m_loading)
	{
		// Keep the region and the slices over the same audio: positions are in playing order,
		// which has just been flipped
		const float start = m_startPointModel.value();
		const float end = m_endPointModel.value();
		const float shift = (1.f - end) - start;
		const float loop = m_loopPointModel.value();
		// Move the leading point first so the points don't push each other around
		if (shift > 0.f)
		{
			m_endPointModel.setValue(end + shift);
			m_loopPointModel.setValue(loop + shift);
			m_startPointModel.setValue(start + shift);
		}
		else
		{
			m_startPointModel.setValue(start + shift);
			m_loopPointModel.setValue(loop + shift);
			m_endPointModel.setValue(end + shift);
		}

		std::vector<float> slices;
		for (float slice : m_slices) { slices.push_back(1.f - slice); }
		std::sort(slices.begin(), slices.end());
		Engine::audioEngine()->requestChangeInModel();
		m_slices = std::move(slices);
		Engine::audioEngine()->doneChangeInModel();
		// Transients of reversed audio are different hits
		if (m_sliceMethodModel.value() == static_cast<int>(SliceMethod::Transients)) { scheduleSlicing(); }
		emit slicesChanged();
	}
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
