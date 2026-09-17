/* Copyright (c) 2026 LMMS developers. SPDX-License-Identifier: GPL-2.0-or-later */
#include <QtTest>
#include "MidiNotePitch.h"

using namespace lmms;

class MidiNotePitchTest : public QObject
{
	Q_OBJECT
private slots:
	void rangeAndCenter()
	{
		QCOMPARE(MidiNotePitch::bend(0, 12), 8192);
		QCOMPARE(MidiNotePitch::bend(12, 12), 16383);
		QCOMPARE(MidiNotePitch::bend(-12, 12), 0);
		QCOMPARE(MidiNotePitch::bend(6, 12), 12288);
		QCOMPARE(MidiNotePitch::bend(-6, 12), 4096);
		QCOMPARE(MidiNotePitch::bend(60, 2), 16383);
		QCOMPARE(MidiNotePitch::bend(-60, 2), 0);
		QCOMPARE(MidiNotePitch::bend(0, 0), 8192);
	}

	void overlapAndRelease()
	{
		MidiNotePitch pitch;
		int first, second;
		pitch.start(0, &first, 2);
		pitch.start(0, &second, -4);
		QCOMPARE(pitch.detuning(0), -4.f);
		QVERIFY(!pitch.update(0, &first, 7));
		QCOMPARE(pitch.detuning(0), -4.f);
		QVERIFY(pitch.end(0, &second));
		QCOMPARE(pitch.detuning(0), 7.f);
		QVERIFY(pitch.end(0, &first));
		QVERIFY(!pitch.hasNotes(0));
		QCOMPARE(pitch.detuning(0), 0.f);
		QVERIFY(!pitch.end(0, &first));
	}

	void channelIsolationAndEarlyRelease()
	{
		MidiNotePitch pitch;
		int first, second, third;
		pitch.start(0, &first, 2);
		pitch.start(0, &second, 4);
		pitch.start(15, &third, -12);
		QVERIFY(!pitch.end(0, &first));
		QCOMPARE(pitch.detuning(0), 4.f);
		QVERIFY(!pitch.update(0, &second, 4));
		QVERIFY(pitch.update(0, &second, 6));
		QCOMPARE(pitch.detuning(15), -12.f);
		QVERIFY(!pitch.update(1, &second, 8));
		QVERIFY(pitch.end(0, &second));
		QVERIFY(pitch.hasNotes(15));
	}
};

QTEST_GUILESS_MAIN(MidiNotePitchTest)
#include "MidiNotePitchTest.moc"
