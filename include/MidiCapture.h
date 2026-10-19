/*
 * MidiCapture.h - always-on MIDI recording: turn what you just played into a clip
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

#ifndef LMMS_GUI_MIDI_CAPTURE_H
#define LMMS_GUI_MIDI_CAPTURE_H

#include <QElapsedTimer>
#include <QObject>
#include <map>
#include <vector>

#include "lmms_export.h"
#include "LmmsTypes.h"

namespace lmms
{

class InstrumentTrack;
class MidiClip;
class Note;
class Track;

namespace gui
{

//! Listens to the live MIDI input of every instrument track in the song and the Pattern Editor, even
//! when nothing is recording. capture() turns the most recent take into a clip (or, for a Pattern Editor
//! track, adds it to the current pattern), so a good idea played while noodling is never lost.
class LMMS_EXPORT MidiCapture : public QObject
{
	Q_OBJECT
public:
	struct CapturedNote
	{
		int key = 0;
		volume_t volume = 100;
		panning_t panning = 0;
		qint64 startMs = 0;
		qint64 endMs = -1;          //!< -1 while the key is still held
		double startTicks = -1;     //!< song position when played during playback, else -1
		double endTicks = -1;
		int session = 0;            //!< changes whenever playback starts or stops
	};

	//! A take converted to musical time, relative to @p startTicks
	struct Take
	{
		double startTicks = 0;      //!< absolute song position (playback) or 0 (free playing)
		bool fromPlayback = false;
		std::size_t firstIndex = 0; //!< index of the take's first note in the input
		struct Entry { int key; volume_t volume; panning_t panning; double pos; double length; };
		std::vector<Entry> notes;
	};

	//! Pauses longer than this split a free-played take
	static constexpr qint64 TakeGapMs = 4000;

	explicit MidiCapture(QObject* parent = nullptr);

	//! The most recent take from @p notes (sorted by start). @p nowMs closes held notes and
	//! @p ticksPerMs converts free-played timing to song time.
	static Take extractTake(const std::vector<CapturedNote>& notes, qint64 nowMs, double ticksPerMs);

	//! Create a clip from the latest take on the most recently played track. On success the clip is
	//! returned and @p message describes what happened; otherwise @p message explains why not.
	MidiClip* capture(QString* message = nullptr);

	bool hasCapture() const;
	InstrumentTrack* lastTrack() const { return m_lastTrack; }
	//! What capture() would do right now, for tooltips/status text
	QString pendingDescription() const;

	//! Exposed for tests: feed notes as if they came from MIDI input
	void notePressed(InstrumentTrack* track, const Note& note);
	void noteReleased(InstrumentTrack* track, const Note& note);

signals:
	void availabilityChanged(bool available);
	//! The take capture() would recover changed (see pendingDescription())
	void pendingChanged();

private slots:
	void watchTrack(lmms::Track* track);
	void onNoteOn(const lmms::Note& note);
	void onNoteOff(const lmms::Note& note);

private:
	void prune(std::vector<CapturedNote>& notes);

	QElapsedTimer m_clock;
	std::map<InstrumentTrack*, std::vector<CapturedNote>> m_notes;
	InstrumentTrack* m_lastTrack = nullptr;
	int m_session = 0;
};

} // namespace gui

} // namespace lmms

#endif // LMMS_GUI_MIDI_CAPTURE_H
