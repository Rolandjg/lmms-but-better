/*
 * MIDI channel pitch state for note detuning.
 * Copyright (c) 2026 LMMS developers
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef LMMS_MIDI_NOTE_PITCH_H
#define LMMS_MIDI_NOTE_PITCH_H

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace lmms
{

// MIDI 1 pitch bend is channel-wide. The most recently started glide
// controls the channel; releasing it restores the previous glide.
// The caller serializes access and owns the note identities.
class MidiNotePitch
{
public:
	static int bend(float semitones, int range)

	{
		const float normalized = std::clamp(semitones / std::max(1, range), -1.f, 1.f);
		return 8192 + std::lround(normalized * (normalized < 0 ? 8192 : 8191));
	}

	void start(int channel, const void* note, float detuning)
	{
		m_notes[channel].push_back({note, detuning});
	}

	bool update(int channel, const void* note, float detuning)
	{
		auto& notes = m_notes[channel];
		
		for (auto& entry : notes)
		{
			if (entry.note != note) { continue; }

			const bool changed = entry.detuning != detuning;
			entry.detuning = detuning;

			return changed && &entry == &notes.back();
		}

		return false;
	}

	bool end(int channel, const void* note)
	{
		auto& notes = m_notes[channel];

		const bool wasActive = !notes.empty() && notes.back().note == note;
		std::erase_if(notes, [note](const auto& entry) { return entry.note == note; });

		return wasActive;
	}

	float detuning(int channel) const
	{
		return hasNotes(channel) ? m_notes[channel].back().detuning : 0.f;
	}

	bool hasNotes(int channel) const { return !m_notes[channel].empty(); }

private:
	struct Entry { const void* note; float detuning; };
	std::array<std::vector<Entry>, 16> m_notes;
};

} // namespace lmms
#endif
