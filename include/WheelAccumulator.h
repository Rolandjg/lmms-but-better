/*
 * WheelAccumulator.h - turns wheel/trackpad deltas into whole steps
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

#ifndef LMMS_GUI_WHEEL_ACCUMULATOR_H
#define LMMS_GUI_WHEEL_ACCUMULATOR_H

#include <QWheelEvent>

namespace lmms::gui
{

/**
 * A mouse wheel sends one event of 120 units per notch, but a trackpad sends a
 * stream of tiny deltas (often dozens per gesture). Code that performs one
 * discrete action per event (zoom one level, bump a knob one step) therefore
 * goes from one end of its range to the other on the slightest touch.
 *
 * This collects the deltas and only reports a step once a full notch's worth
 * has been scrolled, so trackpads and mice behave the same. macOS momentum
 * (the inertial "coasting" after lifting your fingers) is dropped by default,
 * since it otherwise keeps zooming long after the gesture ended.
 */
class WheelAccumulator
{
public:
	static constexpr int DefaultUnitsPerStep = 120;

	//! Feeds \p delta (normally angleDelta().y()) and returns the number of
	//! whole steps to apply now, signed. The remainder carries to the next call.
	int steps(const QWheelEvent* we, int delta, int unitsPerStep = DefaultUnitsPerStep, bool allowMomentum = false)
	{
		if (!allowMomentum && we->phase() == Qt::ScrollMomentum) { return 0; }

		const auto timestamp = we->timestamp();
		const bool stale = timestamp - m_lastTimestamp > ResetAfterMs;
		const bool reversed = (delta > 0 && m_remainder < 0) || (delta < 0 && m_remainder > 0);
		if (stale || reversed || we->phase() == Qt::ScrollBegin) { m_remainder = 0; }
		m_lastTimestamp = timestamp;

		m_remainder += delta;
		const int result = m_remainder / unitsPerStep;
		m_remainder -= result * unitsPerStep;
		return result;
	}

	int steps(const QWheelEvent* we) { return steps(we, we->angleDelta().y()); }

	void reset() { m_remainder = 0; }

private:
	static constexpr unsigned long ResetAfterMs = 300;

	int m_remainder = 0;
	unsigned long m_lastTimestamp = 0;
};

} // namespace lmms::gui

#endif // LMMS_GUI_WHEEL_ACCUMULATOR_H
