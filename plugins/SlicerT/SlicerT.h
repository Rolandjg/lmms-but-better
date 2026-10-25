/*
 * SlicerT.h - declaration of class SlicerT
 *
 * Copyright (c) 2023 Daniel Kauss Serna <daniel.kauss.serna@gmail.com>
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

#ifndef LMMS_SLICERT_H
#define LMMS_SLICERT_H

#include <atomic>
#include <cstdint>

#include "AutomatableModel.h"
#include "ComboBoxModel.h"
#include "Instrument.h"
#include "Note.h"
#include "Sample.h"
#include "SlicerTView.h"

namespace lmms {

class SlicerT : public Instrument
{
	Q_OBJECT

public slots:
	void updateFile(QString file);
	void updateSlices();

signals:
	void isPlaying(float current, float start, float end);

public:
	enum class SliceMode
	{
		Transients,
		Grid,
		Equal,
	};

	SlicerT(InstrumentTrack* instrumentTrack);

	void playNote(NotePlayHandle* handle, SampleFrame* workingBuffer) override;
	void deleteNotePluginData(NotePlayHandle* handle) override;
	float desiredReleaseTimeMs() const override;
	f_cnt_t beatLen(NotePlayHandle* handle) const override;

	void saveSettings(QDomDocument& document, QDomElement& element) override;
	void loadSettings(const QDomElement& element) override;

	void loadFile(const QString& file) override;
	void findSlices();
	void findBPM();

	QString getSampleName() { return m_originalSample.sampleFile(); }

	QString nodeName() const override;
	gui::PluginView* instantiateView(QWidget* parent) override;

	std::vector<Note> getMidi();

	// Slice editing from the GUI. Notes read the slices while playing, so these lock the audio engine.
	void addSlicePoint(float position);
	void removeSlicePoint(std::size_t index);
	void moveSlicePoint(std::size_t index, float position);
	//! Puts dragged points back in order and marks the project as changed
	void finishSliceEdit();
	void clearSlices();

	//! Index of the slice containing @p position, or -1
	int sliceAt(float position) const;
	//! Key that plays the first slice; the key below it plays the whole sample
	int firstSliceKey() const;

	//! Sets the part of the sample (0 to 1) shown in the editor. Slicing again only replaces the slices inside it.
	void setSliceRange(float start, float end);
	float sliceRangeStart() const { return m_sliceRangeStart; }
	float sliceRangeEnd() const { return m_sliceRangeEnd; }
	bool isSliceRangePartial() const { return m_sliceRangeStart > 0.f || m_sliceRangeEnd < 1.f; }

private:
	struct NoteState;

	void setSlicePoints(std::vector<float> slicePoints);
	void sliceSettingChanged();
	//! Slice start frames found by transient detection between @p from and @p to, not including @p from
	std::vector<float> transientFrames(int from, int to) const;
	//! Gets the bounds of the slice @p key plays (0 to 1), false if it plays nothing
	bool sliceBounds(int key, float& start, float& end) const;
	//! Ratio passed to Sample::play (output frames per source frame), from BPM sync and the track pitch
	double playbackRatio() const;

	FloatModel m_noteThreshold;
	FloatModel m_fadeOutFrames;
	IntModel m_originalBPM;
	ComboBoxModel m_sliceSnap;
	BoolModel m_enableSync;
	ComboBoxModel m_sliceMode;
	IntModel m_sliceCount;
	BoolModel m_choke;
	BoolModel m_oneShot;

	Sample m_originalSample;

	std::vector<float> m_slicePoints;
	float m_sliceRangeStart = 0.f;
	float m_sliceRangeEnd = 1.f;

	//! Id of the newest note; older notes fade out when choke is on and only the newest one moves the playhead
	std::atomic<std::uint64_t> m_noteCounter = 0;
	//! Set while loading, so setting the models doesn't redo the slices
	bool m_suppressReslice = false;

	InstrumentTrack* m_parentTrack;

	friend class gui::SlicerTView;
	friend class gui::SlicerTWaveform;
};
} // namespace lmms
#endif // LMMS_SLICERT_H
