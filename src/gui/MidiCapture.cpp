/*
 * MidiCapture.cpp - always-on MIDI recording: turn what you just played into a clip
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

#include "MidiCapture.h"

#include <algorithm>
#include <cmath>

#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Note.h"
#include "PatternStore.h"
#include "PatternTrack.h"
#include "Song.h"

namespace lmms::gui
{

namespace
{

constexpr std::size_t MaxNotesPerTrack = 20000;
constexpr double TicksPerBeat = DefaultTicksPerBar / 4.0;

//! Play position for notes played while the song or a pattern is playing, else -1
double playbackTicks()
{
	const auto song = Engine::getSong();
	if (!song->isPlaying()) { return -1; }
	const auto mode = song->playMode();
	if (mode != Song::PlayMode::Song && mode != Song::PlayMode::Pattern) { return -1; }
	return song->getPlayPos(mode).getTicks();
}

bool inPatternEditor(const InstrumentTrack* track)
{
	return track->trackContainer() == Engine::patternStore();
}

}


MidiCapture::MidiCapture(QObject* parent) :
	QObject(parent)
{
	m_clock.start();
	auto song = Engine::getSong();
	connect(song, &TrackContainer::trackAdded, this, &MidiCapture::watchTrack);
	connect(song, &Song::playbackStateChanged, this, [this] { ++m_session; });
	for (auto track : song->tracks()) { watchTrack(track); }
	auto patternStore = Engine::patternStore();
	connect(patternStore, &TrackContainer::trackAdded, this, &MidiCapture::watchTrack);
	for (auto track : patternStore->tracks()) { watchTrack(track); }
}




void MidiCapture::watchTrack(Track* track)
{
	auto instrumentTrack = dynamic_cast<InstrumentTrack*>(track);
	if (!instrumentTrack) { return; }
	// Notes arrive from the MIDI thread, so always hop over to this thread
	connect(instrumentTrack, &InstrumentTrack::midiNoteOn, this, &MidiCapture::onNoteOn,
		static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection));
	connect(instrumentTrack, &InstrumentTrack::midiNoteOff, this, &MidiCapture::onNoteOff,
		static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection));
	connect(instrumentTrack, &QObject::destroyed, this, [this, instrumentTrack] {
		m_notes.erase(instrumentTrack);
		if (m_lastTrack == instrumentTrack) { m_lastTrack = nullptr; }
		emit availabilityChanged(hasCapture());
		emit pendingChanged();
	});
}




void MidiCapture::onNoteOn(const Note& note)
{
	if (auto track = qobject_cast<InstrumentTrack*>(sender())) { notePressed(track, note); }
}




void MidiCapture::onNoteOff(const Note& note)
{
	if (auto track = qobject_cast<InstrumentTrack*>(sender())) { noteReleased(track, note); }
}




void MidiCapture::notePressed(InstrumentTrack* track, const Note& note)
{
	CapturedNote captured;
	captured.key = note.key();
	captured.volume = note.getVolume();
	captured.panning = note.getPanning();
	captured.startMs = m_clock.elapsed();
	captured.session = m_session;
	captured.startTicks = playbackTicks();

	auto& notes = m_notes[track];
	const bool wasAvailable = hasCapture();
	notes.push_back(captured);
	prune(notes);
	m_lastTrack = track;
	if (!wasAvailable) { emit availabilityChanged(true); }
	emit pendingChanged();
}




void MidiCapture::noteReleased(InstrumentTrack* track, const Note& note)
{
	auto it = m_notes.find(track);
	if (it == m_notes.end()) { return; }
	// Close the most recent still-held note on this key
	for (auto n = it->second.rbegin(); n != it->second.rend(); ++n)
	{
		if (n->key != note.key() || n->endMs >= 0) { continue; }
		n->endMs = m_clock.elapsed();
		if (const auto ticks = playbackTicks(); n->startTicks >= 0 && ticks >= 0) { n->endTicks = ticks; }
		break;
	}
}




void MidiCapture::prune(std::vector<CapturedNote>& notes)
{
	if (notes.size() > MaxNotesPerTrack)
	{
		notes.erase(notes.begin(), notes.begin() + static_cast<long>(notes.size() - MaxNotesPerTrack));
	}
}




bool MidiCapture::hasCapture() const
{
	if (!m_lastTrack) { return false; }
	const auto it = m_notes.find(m_lastTrack);
	return it != m_notes.end() && !it->second.empty();
}




QString MidiCapture::pendingDescription() const
{
	if (!hasCapture())
	{
		return tr("Nothing to capture yet: play something on a MIDI keyboard or the on-screen piano, then "
			"capture it even if you weren't recording.");
	}
	const double ticksPerMs = Engine::getSong()->getTempo() * TicksPerBeat / 60000.0;
	const auto count = static_cast<int>(extractTake(m_notes.at(m_lastTrack), m_clock.elapsed(), ticksPerMs).notes.size());
	return inPatternEditor(m_lastTrack)
		? tr("Recover the last %n note(s) you played on \"%1\" into the current pattern", "", count)
			.arg(m_lastTrack->name())
		: tr("Recover the last %n note(s) you played on \"%1\" as a new clip", "", count).arg(m_lastTrack->name());
}




MidiCapture::Take MidiCapture::extractTake(const std::vector<CapturedNote>& notes, qint64 nowMs, double ticksPerMs)
{
	Take take;
	if (notes.empty()) { return take; }

	const auto endMsOf = [nowMs](const CapturedNote& n) { return n.endMs >= 0 ? n.endMs : nowMs; };
	const CapturedNote& last = notes.back();
	take.fromPlayback = last.startTicks >= 0;

	// Walk back from the newest note to where the take began
	std::size_t first = notes.size() - 1;
	qint64 earliestStart = last.startMs;
	while (first > 0)
	{
		const CapturedNote& prev = notes[first - 1];
		if (prev.session != last.session) { break; }
		if ((prev.startTicks >= 0) != take.fromPlayback) { break; }
		if (take.fromPlayback)
		{
			// The loop wrapped around: keep only the latest pass
			if (prev.startTicks > notes[first].startTicks + TicksPerBeat) { break; }
		}
		else if (earliestStart - endMsOf(prev) > TakeGapMs)
		{
			break;
		}
		earliestStart = std::min(earliestStart, prev.startMs);
		--first;
	}
	take.firstIndex = first;

	const qint64 originMs = notes[first].startMs;
	take.startTicks = take.fromPlayback ? notes[first].startTicks : 0.0;
	for (std::size_t i = first; i < notes.size(); ++i)
	{
		const CapturedNote& n = notes[i];
		const double msLength = static_cast<double>(endMsOf(n) - n.startMs) * ticksPerMs;
		double pos;
		double length;
		if (take.fromPlayback)
		{
			pos = n.startTicks;
			length = n.endTicks > n.startTicks ? n.endTicks - n.startTicks : msLength;
		}
		else
		{
			pos = static_cast<double>(n.startMs - originMs) * ticksPerMs;
			length = msLength;
		}
		take.startTicks = std::min(take.startTicks, pos);
		take.notes.push_back({n.key, n.volume, n.panning, pos, std::max(1.0, length)});
	}
	return take;
}




MidiClip* MidiCapture::capture(QString* message)
{
	const auto setMessage = [message](const QString& text) { if (message) { *message = text; } };
	if (!hasCapture())
	{
		setMessage(tr("Nothing to capture yet - play something on a MIDI keyboard or the on-screen piano first."));
		return nullptr;
	}

	const auto song = Engine::getSong();
	InstrumentTrack* track = m_lastTrack;
	auto& notes = m_notes[track];
	const double ticksPerMs = song->getTempo() * TicksPerBeat / 60000.0;
	const Take take = extractTake(notes, m_clock.elapsed(), ticksPerMs);
	if (take.notes.empty())
	{
		setMessage(tr("Nothing to capture yet."));
		return nullptr;
	}

	const tick_t ticksPerBar = TimePos::ticksPerBar();
	if (inPatternEditor(track))
	{
		// Pattern Editor tracks have one clip per pattern: add the take to the current pattern
		const auto patternStore = Engine::patternStore();
		const int pattern = patternStore->currentPattern();
		auto clip = dynamic_cast<MidiClip*>(track->getClip(pattern));
		if (!clip)
		{
			setMessage(tr("Could not find the current pattern's clip."));
			return nullptr;
		}
		const double patternTicks = std::max(1, patternStore->lengthOfPattern(pattern)) * ticksPerBar;
		clip->addJournalCheckPoint();
		for (const auto& entry : take.notes)
		{
			// Played along with the pattern: keep the position in it; otherwise start at the beginning
			const double pos = take.fromPlayback ? std::fmod(entry.pos, patternTicks) : entry.pos - take.startTicks;
			const auto length = static_cast<tick_t>(std::max<long>(1, std::lround(entry.length)));
			clip->addNote(Note(TimePos(length), TimePos(static_cast<tick_t>(std::max(0l, std::lround(pos)))), entry.key,
				entry.volume, entry.panning), false);
		}
		clip->rearrangeAllNotes();
		clip->updateLength();
		song->setModified();
		const auto patternTrack = PatternTrack::findPatternTrack(pattern);
		setMessage(tr("Captured %n note(s) into pattern \"%1\" on \"%2\"", "", static_cast<int>(take.notes.size()))
			.arg(patternTrack ? patternTrack->name() : QString::number(pattern + 1), track->name()));
		notes.erase(notes.begin() + static_cast<long>(take.firstIndex), notes.end());
		emit availabilityChanged(hasCapture());
		emit pendingChanged();
		return clip;
	}

	TimePos clipStart;
	if (take.fromPlayback)
	{
		// Keep the notes where they were played in the song
		clipStart = static_cast<tick_t>(std::floor(take.startTicks / ticksPerBar)) * ticksPerBar;
	}
	else
	{
		// Free playing lands at the song position marker, on a bar line
		clipStart = song->getPlayPos(Song::PlayMode::Song).getBar() * ticksPerBar;
	}

	track->addJournalCheckPoint();
	auto clip = dynamic_cast<MidiClip*>(track->createClip(clipStart));
	if (!clip)
	{
		setMessage(tr("Could not create a clip."));
		return nullptr;
	}
	const double offset = take.fromPlayback ? clipStart.getTicks() : take.startTicks;
	for (const auto& entry : take.notes)
	{
		const auto pos = static_cast<tick_t>(std::lround(entry.pos - offset));
		const auto length = static_cast<tick_t>(std::max<long>(1, std::lround(entry.length)));
		clip->addNote(Note(TimePos(length), TimePos(std::max<tick_t>(0, pos)), entry.key, entry.volume, entry.panning),
			false);
	}
	clip->updateLength();
	clip->rearrangeAllNotes();
	song->setModified();

	setMessage(tr("Captured %n note(s) to \"%1\"", "", static_cast<int>(take.notes.size())).arg(track->name()));

	// The take is used up; capturing again picks the take before it
	notes.erase(notes.begin() + static_cast<long>(take.firstIndex), notes.end());
	emit availabilityChanged(hasCapture());
	emit pendingChanged();
	return clip;
}

} // namespace lmms::gui
