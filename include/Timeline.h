/*
 * Timeline.h
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

#ifndef LMMS_TIMELINE_H
#define LMMS_TIMELINE_H

#include <QColor>
#include <QObject>
#include <QString>
#include <vector>

#include "AudioEngine.h"
#include "Engine.h"
#include "JournallingObject.h"
#include "TimePos.h"

namespace lmms {

class Timeline : public QObject, public JournallingObject
{
	Q_OBJECT

public:
	enum class StopBehaviour
	{
		BackToZero,
		BackToStart,
		KeepPosition
	};

	auto pos() const -> const TimePos& { return m_pos; }

	auto ticks() const -> tick_t { return m_pos.getTicks(); }

	//! Forcefully sets the current ticks, resets the frame offset, and sets the elapsed seconds based on the global position (ignoring potential mid-song tempo changes)
	//! Unless `jumped` is passed as false, this function will emit the `positionJumped` signal to allow other widgets to update accordingly.
	void setTicks(tick_t ticks, bool jumped = true)
	{
		m_pos.setTicks(ticks);
		m_frameOffset = 0;
		m_elapsedSeconds = ticks * Engine::framesPerTick() / Engine::audioEngine()->outputSampleRate();
		if (jumped) { emit positionJumped(); }
		emit positionChanged();
	}

	//! Advances the current timeline position by a certain number of ticks, in addition to updating the elapsed time based on the current tempo.
	void incrementTicks(tick_t increment)
	{
		m_pos.setTicks(ticks() + increment);
		m_elapsedSeconds += increment * Engine::framesPerTick() / Engine::audioEngine()->outputSampleRate();
		emit positionChanged();
	}

	auto frameOffset() const -> float { return m_frameOffset; }
	void setFrameOffset(const float frame) { m_frameOffset = frame; }

	auto loopBegin() const -> TimePos { return m_loopBegin; }
	auto loopEnd() const -> TimePos { return m_loopEnd; }
	auto loopEnabled() const -> bool { return m_loopEnabled; }

	void setLoopBegin(TimePos begin);
	void setLoopEnd(TimePos end);
	void setLoopPoints(TimePos begin, TimePos end);
	void setLoopEnabled(bool enabled);

	auto playStartPosition() const -> TimePos { return m_playStartPosition; }
	auto stopBehaviour() const -> StopBehaviour { return m_stopBehaviour; }

	void setPlayStartPosition(TimePos position) { m_playStartPosition = position; }
	void setStopBehaviour(StopBehaviour behaviour);

	//! A named position in the arrangement, such as "Chorus"
	struct Marker
	{
		TimePos pos;
		QString name;
		QColor color;
	};

	//! Markers sorted by position
	auto markers() const -> const std::vector<Marker>& { return m_markers; }
	//! Adds a marker and returns its index in markers()
	auto addMarker(TimePos pos, const QString& name, const QColor& color) -> int;
	void removeMarker(int index);
	void clearMarkers() { m_markers.clear(); emit markersChanged(); }
	//! Moves a marker; returns its new index, as the list stays sorted
	auto moveMarker(int index, TimePos pos) -> int;
	void renameMarker(int index, const QString& name);
	void setMarkerColor(int index, const QColor& color);
	//! Index of the first marker after / last marker before @p pos, or -1
	auto nextMarker(TimePos pos) const -> int;
	auto previousMarker(TimePos pos) const -> int;
	//! End of the section that starts at marker @p index: the next marker, or -1 for the last one
	auto sectionEnd(int index) const -> TimePos;
	//! Moves every marker at or after @p from by @p ticks (bars inserted or removed there); markers
	//! inside a removed range end up at @p from. Returns whether any marker moved.
	auto shiftMarkers(TimePos from, tick_t ticks) -> bool;

	auto getElapsedSeconds() const -> double { return m_elapsedSeconds + frameOffset() / Engine::audioEngine()->outputSampleRate(); }

	auto nodeName() const -> QString override { return "timeline"; }

signals:
	void loopEnabledChanged(bool enabled);
	void stopBehaviourChanged(lmms::Timeline::StopBehaviour behaviour);
	void positionChanged();
	void positionJumped();
	void markersChanged();

protected:
	void saveSettings(QDomDocument& doc, QDomElement& element) override;
	void loadSettings(const QDomElement& element) override;

private:
	TimePos m_loopBegin = TimePos{0};
	TimePos m_loopEnd = TimePos{DefaultTicksPerBar};
	bool m_loopEnabled = false;
	TimePos m_pos = TimePos{0};

	float m_frameOffset = 0;

	double m_elapsedSeconds = 0;

	std::vector<Marker> m_markers;

	StopBehaviour m_stopBehaviour = StopBehaviour::BackToStart;
	TimePos m_playStartPosition = TimePos{-1};
};

} // namespace lmms

#endif // LMMS_TIMELINE_H
