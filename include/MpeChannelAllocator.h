/*
 * MPE member channel allocation for per-note pitch bends.
 * Copyright (c) 2026 LMMS developers
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef LMMS_MPE_CHANNEL_ALLOCATOR_H
#define LMMS_MPE_CHANNEL_ALLOCATOR_H

#include <algorithm>
#include <array>
#include <cstdint>

namespace lmms
{

// Gives each note its own MPE lower zone member channel (indices 1..count),
// so a pitch bend only reaches that note. Channel 0 is the master channel and
// is never handed out. The caller serializes access and owns the note identities.
class MpeChannelAllocator
{
public:
	static constexpr int MaxMemberChannels = 15;

	explicit MpeChannelAllocator(int count = MaxMemberChannels) { setMemberCount(count); }

	void setMemberCount(int count) { m_count = std::clamp(count, 1, MaxMemberChannels); }
	int memberCount() const { return m_count; }

	// Prefers the free channel released longest ago, so release tails keep
	// their bend as long as possible. When every channel sounds, the channel
	// with the oldest note-on is shared with the new note.
	int allocate(const void* note)
	{
		int best = 0;
		for (int channel = 1; channel <= m_count; ++channel)
		{
			const auto& state = m_channels[channel];
			if (best == 0) { best = channel; continue; }

			const auto& current = m_channels[best];
			const bool free = state.note == nullptr, currentFree = current.note == nullptr;
			if (free != currentFree ? free : state.stamp < current.stamp) { best = channel; }
		}

		m_channels[best] = {note, ++m_clock};
		return best;
	}

	void release(const void* note)
	{
		for (auto& state : m_channels)
		{
			if (state.note == note) { state = {nullptr, ++m_clock}; }
		}
	}

	bool owns(int channel, const void* note) const
	{
		return channel > 0 && channel <= MaxMemberChannels && note && m_channels[channel].note == note;
	}

	void reset() { m_channels = {}; }

private:
	struct Channel { const void* note = nullptr; std::uint64_t stamp = 0; };
	std::array<Channel, MaxMemberChannels + 1> m_channels{};
	std::uint64_t m_clock = 0;
	int m_count = MaxMemberChannels;
};

} // namespace lmms
#endif
