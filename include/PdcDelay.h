/*
 * PdcDelay.h - delay line for plugin delay compensation
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

#ifndef LMMS_PDC_DELAY_H
#define LMMS_PDC_DELAY_H

#include <algorithm>
#include <vector>

#include "AudioBufferView.h"
#include "LmmsTypes.h"
#include "SampleFrame.h"

namespace lmms
{

//! Holds back a signal path that is earlier than the others it is mixed with, so plugins with
//! processing latency (lookahead limiters, linear-phase EQs, ...) don't put parallel paths out of time.
class PdcDelay
{
public:
	f_cnt_t delay() const { return m_delay; }

	//! Changing the delay clears the history, so only call this when the latency actually changed
	void setDelay(f_cnt_t frames)
	{
		if (frames == m_delay) { return; }
		m_delay = frames;
		m_buffer.assign(frames, SampleFrame{});
		m_pos = 0;
	}

	void clear() { std::fill(m_buffer.begin(), m_buffer.end(), SampleFrame{}); }

	//! Delays @p frames frames of @p buffer in place
	void process(SampleFrame* buffer, f_cnt_t frames)
	{
		if (m_delay == 0) { return; }
		for (f_cnt_t f = 0; f < frames; ++f)
		{
			std::swap(buffer[f], m_buffer[m_pos]);
			if (++m_pos == m_delay) { m_pos = 0; }
		}
	}

	//! Delays the first two channels of a planar buffer in place
	void process(PlanarBufferView<float> buffer)
	{
		if (m_delay == 0) { return; }
		for (f_cnt_t f = 0; f < buffer.frames(); ++f)
		{
			auto& stored = m_buffer[m_pos];
			std::swap(buffer.sample(0, f), stored[0]);
			std::swap(buffer.sample(1, f), stored[1]);
			if (++m_pos == m_delay) { m_pos = 0; }
		}
	}

private:
	std::vector<SampleFrame> m_buffer;
	f_cnt_t m_delay = 0;
	f_cnt_t m_pos = 0;
};

} // namespace lmms

#endif // LMMS_PDC_DELAY_H
