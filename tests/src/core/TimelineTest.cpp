/*
 * TimelineTest.cpp
 *
 * Copyright (c) 2025 Keratin
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


#include <QDomDocument>
#include <QtTest>
#include "Timeline.h"

#include "Song.h"

class TimelineTest : public QObject
{
	Q_OBJECT
public:
	bool positionChangedReceived;
	bool positionJumpedReceived;
	void resetReceived()
	{
		positionChangedReceived = false;
		positionJumpedReceived = false;
	}

private slots:
	void onPositionChanged() { positionChangedReceived = true; }
	void onPositionJumped() { positionJumpedReceived = true; }

private slots:

	void initTestCase()
	{
		using namespace lmms;
		Engine::init(true);
	}

	void cleanupTestCase()
	{
		using namespace lmms;
		Engine::destroy();
	}

	void JumpedTests()
	{
		using namespace lmms;

		Timeline timeline = Timeline();
		connect(&timeline, &Timeline::positionChanged, this, &TimelineTest::onPositionChanged);
		connect(&timeline, &Timeline::positionJumped, this, &TimelineTest::onPositionJumped);

		// By default, setting the ticks is treated as a forceful jump
		resetReceived();
		timeline.setTicks(10);
		QCOMPARE(timeline.ticks(), 10);
		QVERIFY(positionChangedReceived);
		QVERIFY(positionJumpedReceived);

		// However, using incrementTicks will not emit positionJumped.
		resetReceived();
		timeline.incrementTicks(10);
		QCOMPARE(timeline.ticks(), 20);
		QVERIFY(positionChangedReceived);
		QVERIFY(!positionJumpedReceived);
	}

	void ElapsedTimeTests()
	{
		using namespace lmms;

		Timeline timeline = Timeline();
		connect(&timeline, &Timeline::positionChanged, this, &TimelineTest::onPositionChanged);
		connect(&timeline, &Timeline::positionJumped, this, &TimelineTest::onPositionJumped);

		// Forecefully setting the ticks to 0 should reset the elapsed time
		timeline.setTicks(0);
		QCOMPARE(timeline.getElapsedSeconds(), 0);

		// Setting the ticks to a nonzero value should reset the elapsed time to that tick's time based on the current tempo
		Engine::getSong()->setTempo(240);
		double secondsPerTick = 60.0f / Engine::getSong()->getTempo() * 4 / DefaultTicksPerBar;
		timeline.setTicks(10);
		double initialElapsedSeconds = timeline.getElapsedSeconds();
		QCOMPARE(static_cast<int>(timeline.getElapsedSeconds() * 1000), static_cast<int>(10 * secondsPerTick * 1000)); // Rouding to milliseconds to prevent double comparison issues

		// Changing the tempo and then non-forcefully incrementing the ticks will increase the elapsed time based on the new tempo
		Engine::getSong()->setTempo(60);
		secondsPerTick = 60.0f / Engine::getSong()->getTempo() * 4 / DefaultTicksPerBar;
		timeline.incrementTicks(5);
		QCOMPARE(static_cast<int>(timeline.getElapsedSeconds() * 1000), static_cast<int>((initialElapsedSeconds + 5 * secondsPerTick) * 1000));

		// Forcefully setting the ticks (such as dragging the playhead with the mouse) will reset the elapsed time based on the global position and current tempo
		Engine::getSong()->setTempo(180);
		secondsPerTick = 60.0f / Engine::getSong()->getTempo() * 4 / DefaultTicksPerBar;
		timeline.setTicks(25);
		QCOMPARE(static_cast<int>(timeline.getElapsedSeconds() * 1000), static_cast<int>(25 * secondsPerTick * 1000));
	}


	void markersStaySortedAndRoundTrip()
	{
		using namespace lmms;
		auto& timeline = Engine::getSong()->getTimeline(Song::PlayMode::Song);
		timeline.clearMarkers();
		timeline.addMarker(TimePos{384}, "Chorus", QColor("#e0544e"));
		timeline.addMarker(TimePos{0}, "Intro", QColor("#5b8def"));
		const int verse = timeline.addMarker(TimePos{192}, "Verse", QColor("#3fbf8f"));
		QCOMPARE(verse, 1);
		QCOMPARE(timeline.markers()[0].name, QString("Intro"));
		QCOMPARE(timeline.markers()[2].name, QString("Chorus"));

		// Moving past another marker re-sorts
		const int moved = timeline.moveMarker(0, TimePos{500});
		QCOMPARE(moved, 2);
		QCOMPARE(timeline.markers()[0].name, QString("Verse"));

		QCOMPARE(timeline.nextMarker(TimePos{192}), 1);
		QCOMPARE(timeline.previousMarker(TimePos{192}), -1);
		QCOMPARE(timeline.previousMarker(TimePos{400}), 1);
		QCOMPARE(static_cast<int>(timeline.sectionEnd(0)), 384);
		QCOMPARE(static_cast<int>(timeline.sectionEnd(2)), -1);

		QDomDocument doc;
		auto element = doc.createElement("timeline");
		timeline.saveState(doc, element);
		timeline.clearMarkers();
		QVERIFY(timeline.markers().empty());
		timeline.restoreState(element.firstChildElement());
		QCOMPARE(timeline.markers().size(), std::size_t{3});
		QCOMPARE(timeline.markers()[2].name, QString("Intro"));
		QCOMPARE(static_cast<int>(timeline.markers()[2].pos), 500);
		QCOMPARE(timeline.markers()[1].color, QColor("#e0544e"));
		timeline.clearMarkers();
	}

	void markersFollowInsertedAndRemovedBars()
	{
		using namespace lmms;
		auto& timeline = Engine::getSong()->getTimeline(Song::PlayMode::Song);
		timeline.clearMarkers();
		timeline.addMarker(TimePos{0}, "Intro", QColor());
		timeline.addMarker(TimePos{200}, "Verse", QColor());
		timeline.addMarker(TimePos{400}, "Chorus", QColor());

		QVERIFY(timeline.shiftMarkers(TimePos{192}, 192));
		QCOMPARE(static_cast<int>(timeline.markers()[0].pos), 0);
		QCOMPARE(static_cast<int>(timeline.markers()[1].pos), 392);
		QCOMPARE(static_cast<int>(timeline.markers()[2].pos), 592);

		// a marker inside the removed bar lands on the cut, and the list stays sorted
		QVERIFY(timeline.shiftMarkers(TimePos{300}, -192));
		QCOMPARE(static_cast<int>(timeline.markers()[1].pos), 300);
		QCOMPARE(static_cast<int>(timeline.markers()[2].pos), 400);
		QVERIFY(!timeline.shiftMarkers(TimePos{1000}, 192));
		QCOMPARE(static_cast<int>(timeline.sectionEnd(-1)), -1);
		QCOMPARE(static_cast<int>(timeline.sectionEnd(3)), -1);
		timeline.clearMarkers();
	}
};

QTEST_GUILESS_MAIN(TimelineTest)
#include "TimelineTest.moc"
