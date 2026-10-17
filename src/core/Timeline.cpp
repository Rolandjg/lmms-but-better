/*
 * Timeline.cpp
 *
 * Copyright (c) 2023 Dominic Clark
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
 */

#include "Timeline.h"

#include <algorithm>
#include <tuple>

#include <QDomElement>

namespace lmms {

void Timeline::setLoopBegin(TimePos begin)
{
	std::tie(m_loopBegin, m_loopEnd) = std::minmax(begin, TimePos{m_loopEnd});
}

void Timeline::setLoopEnd(TimePos end)
{
	std::tie(m_loopBegin, m_loopEnd) = std::minmax(TimePos{m_loopBegin}, end);
}

void Timeline::setLoopPoints(TimePos begin, TimePos end)
{
	std::tie(m_loopBegin, m_loopEnd) = std::minmax(begin, end);
}

void Timeline::setLoopEnabled(bool enabled)
{
	if (enabled != m_loopEnabled) {
		m_loopEnabled = enabled;
		emit loopEnabledChanged(m_loopEnabled);
	}
}

void Timeline::setStopBehaviour(StopBehaviour behaviour)
{
	if (behaviour != m_stopBehaviour) {
		m_stopBehaviour = behaviour;
		emit stopBehaviourChanged(m_stopBehaviour);
	}
}

auto Timeline::addMarker(TimePos pos, const QString& name, const QColor& color) -> int
{
	pos = std::max(pos, TimePos{0});
	const auto it = std::upper_bound(m_markers.begin(), m_markers.end(), pos,
		[](const TimePos& p, const Marker& m) { return p < m.pos; });
	const auto index = static_cast<int>(it - m_markers.begin());
	m_markers.insert(it, Marker{pos, name, color});
	emit markersChanged();
	return index;
}

void Timeline::removeMarker(int index)
{
	if (index < 0 || index >= static_cast<int>(m_markers.size())) { return; }
	m_markers.erase(m_markers.begin() + index);
	emit markersChanged();
}

auto Timeline::moveMarker(int index, TimePos pos) -> int
{
	if (index < 0 || index >= static_cast<int>(m_markers.size())) { return -1; }
	if (m_markers[index].pos == pos) { return index; }
	auto marker = m_markers[index];
	m_markers.erase(m_markers.begin() + index);
	marker.pos = std::max(pos, TimePos{0});
	const auto it = std::upper_bound(m_markers.begin(), m_markers.end(), marker.pos,
		[](const TimePos& p, const Marker& m) { return p < m.pos; });
	const auto newIndex = static_cast<int>(it - m_markers.begin());
	m_markers.insert(it, marker);
	emit markersChanged();
	return newIndex;
}

void Timeline::renameMarker(int index, const QString& name)
{
	if (index < 0 || index >= static_cast<int>(m_markers.size())) { return; }
	m_markers[index].name = name;
	emit markersChanged();
}

void Timeline::setMarkerColor(int index, const QColor& color)
{
	if (index < 0 || index >= static_cast<int>(m_markers.size())) { return; }
	m_markers[index].color = color;
	emit markersChanged();
}

auto Timeline::nextMarker(TimePos pos) const -> int
{
	for (std::size_t i = 0; i < m_markers.size(); ++i)
	{
		if (m_markers[i].pos > pos) { return static_cast<int>(i); }
	}
	return -1;
}

auto Timeline::previousMarker(TimePos pos) const -> int
{
	for (auto i = static_cast<int>(m_markers.size()) - 1; i >= 0; --i)
	{
		if (m_markers[i].pos < pos) { return i; }
	}
	return -1;
}

auto Timeline::sectionEnd(int index) const -> TimePos
{
	if (index < 0 || index >= static_cast<int>(m_markers.size())) { return TimePos{-1}; }
	for (auto i = static_cast<std::size_t>(index) + 1; i < m_markers.size(); ++i)
	{
		if (m_markers[i].pos > m_markers[index].pos) { return m_markers[i].pos; }
	}
	return TimePos{-1};
}

auto Timeline::shiftMarkers(TimePos from, tick_t ticks) -> bool
{
	bool moved = false;
	for (auto& marker : m_markers)
	{
		if (marker.pos < from) { continue; }
		// clamping to from keeps the list sorted
		const auto pos = TimePos{std::max<tick_t>(from.getTicks(), marker.pos.getTicks() + ticks)};
		moved = moved || pos != marker.pos;
		marker.pos = pos;
	}
	if (moved) { emit markersChanged(); }
	return moved;
}

void Timeline::saveSettings(QDomDocument& doc, QDomElement& element)
{
	element.setAttribute("lp0pos", static_cast<int>(loopBegin()));
	element.setAttribute("lp1pos", static_cast<int>(loopEnd()));
	element.setAttribute("lpstate", static_cast<int>(loopEnabled()));
	element.setAttribute("stopbehaviour", static_cast<int>(stopBehaviour()));
	for (const auto& marker : m_markers)
	{
		auto node = doc.createElement("marker");
		node.setAttribute("pos", static_cast<int>(marker.pos));
		node.setAttribute("name", marker.name);
		node.setAttribute("color", marker.color.name());
		element.appendChild(node);
	}
}

void Timeline::loadSettings(const QDomElement& element)
{
	setLoopPoints(
		static_cast<TimePos>(element.attribute("lp0pos").toInt()),
		static_cast<TimePos>(element.attribute("lp1pos").toInt())
	);
	setLoopEnabled(static_cast<bool>(element.attribute("lpstate").toInt()));
	setStopBehaviour(static_cast<StopBehaviour>(element.attribute("stopbehaviour", "1").toInt()));

	m_markers.clear();
	for (auto node = element.firstChildElement("marker"); !node.isNull(); node = node.nextSiblingElement("marker"))
	{
		m_markers.push_back(Marker{TimePos{std::max(0, node.attribute("pos").toInt())}, node.attribute("name"),
			QColor(node.attribute("color", "#4a9bd9"))});
	}
	std::stable_sort(m_markers.begin(), m_markers.end(), [](const Marker& a, const Marker& b) { return a.pos < b.pos; });
	emit markersChanged();
}

} // namespace lmms
