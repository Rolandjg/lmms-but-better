/*
 * AudioFileProcessor.h - declaration of class AudioFileProcessor
 *                          (instrument-plugin for using audio-files)
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

#ifndef LMMS_AUDIO_FILE_PROCESSOR_H
#define LMMS_AUDIO_FILE_PROCESSOR_H


#include <atomic>
#include <cstdint>
#include <vector>

#include "AutomatableModel.h"
#include "ComboBoxModel.h"

#include "Instrument.h"
#include "Sample.h"
#include "LmmsTypes.h"


namespace lmms
{

class AudioFileProcessor : public Instrument
{
	Q_OBJECT
public:
	AudioFileProcessor( InstrumentTrack * _instrument_track );

	void playNote( NotePlayHandle * _n,
						SampleFrame* _working_buffer ) override;
	void deleteNotePluginData( NotePlayHandle * _n ) override;

	void saveSettings(QDomDocument& doc, QDomElement& elem) override;
	void loadSettings(const QDomElement& elem) override;

	void loadFile( const QString & _file ) override;

	QString nodeName() const override;

	auto beatLen(NotePlayHandle* note) const -> f_cnt_t override;

	float desiredReleaseTimeMs() const override;

	gui::PluginView* instantiateView( QWidget * _parent ) override;

	Sample const & sample() const { return m_sample; }

	FloatModel & ampModel() { return m_ampModel; }
	FloatModel & startPointModel() { return m_startPointModel; }
	FloatModel & endPointModel() { return m_endPointModel; }
	FloatModel & loopPointModel() { return m_loopPointModel; }
	BoolModel & reverseModel() { return m_reverseModel; }
	IntModel & loopModel() { return m_loopModel; }
	BoolModel & stutterModel() { return m_stutterModel; }
	ComboBoxModel & interpolationModel() { return m_interpolationModel; }
	BoolModel & warpModel() { return m_warpModel; }
	IntModel & sampleTempoModel() { return m_sampleTempoModel; }
	FloatModel & crossfadeModel() { return m_crossfadeModel; }
	IntModel & modeModel() { return m_modeModel; }
	IntModel & sliceMethodModel() { return m_sliceMethodModel; }
	FloatModel & sliceSensitivityModel() { return m_sliceSensitivityModel; }
	IntModel & sliceDivisionModel() { return m_sliceDivisionModel; }
	BoolModel & oneShotModel() { return m_oneShotModel; }
	BoolModel & chokeModel() { return m_chokeModel; }
	BoolModel & snapModel() { return m_snapModel; }
	FloatModel & fadeInModel() { return m_fadeInModel; }
	FloatModel & fadeOutModel() { return m_fadeOutModel; }
	FloatModel & transposeModel() { return m_transposeModel; }
	FloatModel & tuneModel() { return m_tuneModel; }

	enum class Mode { Classic, Slice };
	enum class SliceMethod { Transients, Beats, Manual };

	bool sliceMode() const { return m_modeModel.value() == static_cast<int>(Mode::Slice); }

	//! Slice markers inside the sample, as sorted positions in 0..1 (like the start/end points)
	const std::vector<float>& slices() const { return m_slices; }
	//! Replace the markers by hand; switches the slice method to manual
	void setSlices(std::vector<float> slices);
	//! The slice boundaries in frames: region start, the markers inside the region, region end
	std::vector<int> sliceBoundaries() const;
	//! Copy the slices as one note per slice to the clipboard, laid out at the song tempo
	void copySlicesAsNotes() const;

	//! Nearest zero crossing of the playback sample within a few milliseconds of @p frame
	int nearestZeroCrossing(int frame) const;
	//! Set the amplification so the loudest point between start and end reaches 0 dBFS
	void normalize();
	//! Estimate the pitch of the region and set the track's base note and fine tune so it plays
	//! in tune. Returns the detected frequency, or 0 if no clear pitch was found.
	float detectRootKey();
	//! Load the previous (-1) or next (+1) audio file in the current sample's folder
	void stepSampleFile(int direction);

	//! Frame and counter of the most recently rendered note, for the playback cursor
	int playFrame() const { return m_playFrame.load(std::memory_order_relaxed); }
	unsigned playTick() const { return m_playTick.load(std::memory_order_relaxed); }
	int playKey() const { return m_playKey.load(std::memory_order_relaxed); }

	//! Guess the tempo of a loop from a "... 140 BPM" style file name or from its length
	static int detectTempo(const QString& fileName, double seconds);
	//! Onsets of @p sample in frames (logical order), more of them at higher @p sensitivity (0..1)
	static std::vector<int> detectTransients(const Sample& sample, float sensitivity);
	//! Fundamental frequency of mono @p data (YIN), or 0 if it isn't clearly pitched
	static float detectPitch(const float* data, int size, int sampleRate);


public slots:
	void setAudioFile(const QString& _audio_file, bool _rename = true);

private slots:
	void reverseModelChanged();
	void ampModelChanged();
	void loopPointChanged();
	void startPointChanged();
	void endPointChanged();
	void pointChanged();
	void stutterModelChanged();
	//! Coalesces several parameter changes into one rebuild on the GUI thread
	void scheduleRebuild();
	//! Re-run automatic slicing after a parameter change (not for manual slices)
	void scheduleSlicing();


signals:
	void sampleUpdated();
	void slicesChanged();

private:
	//! Derive the playback sample from the source: time-stretch to the song tempo (warp)
	//! and bake the loop crossfade into the loop end
	void rebuildPlaybackSample();
	bool crossfadeActive() const;
	bool oneShotActive() const;
	void updateSlices();
	//! Mono value of the playback sample at logical (as played, so possibly reversed) @p frame
	float monoAt(int frame) const;

	struct NoteState;

	std::shared_ptr<const SampleBuffer> m_sourceBuffer;
	std::vector<SampleFrame> m_warpedFrames;
	double m_warpedRatio = 0.0;
	bool m_rebuildQueued = false;

	Sample m_sample;

	FloatModel m_ampModel;
	FloatModel m_startPointModel;
	FloatModel m_endPointModel;
	FloatModel m_loopPointModel;
	BoolModel m_reverseModel;
	IntModel m_loopModel;
	BoolModel m_stutterModel;
	ComboBoxModel m_interpolationModel;
	BoolModel m_warpModel;
	IntModel m_sampleTempoModel;
	FloatModel m_crossfadeModel;
	IntModel m_modeModel;
	IntModel m_sliceMethodModel;
	FloatModel m_sliceSensitivityModel;
	IntModel m_sliceDivisionModel;
	BoolModel m_oneShotModel;
	BoolModel m_chokeModel;
	BoolModel m_snapModel;
	FloatModel m_fadeInModel;
	FloatModel m_fadeOutModel;
	FloatModel m_transposeModel;
	FloatModel m_tuneModel;

	std::vector<float> m_slices;
	bool m_slicingQueued = false;
	bool m_loading = false;

	f_cnt_t m_nextPlayStartPoint;
	bool m_nextPlayBackwards;

	std::atomic<std::uint64_t> m_noteCounter = 0;
	std::atomic<int> m_playFrame = 0;
	std::atomic<unsigned> m_playTick = 0;
	std::atomic<int> m_playKey = -1;
} ;

} // namespace lmms

#endif // LMMS_AUDIO_FILE_PROCESSOR_H
