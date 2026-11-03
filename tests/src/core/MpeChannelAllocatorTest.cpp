/* Copyright (c) 2026 LMMS developers. SPDX-License-Identifier: GPL-2.0-or-later */
#include <QtTest>
#include <set>
#include "MpeChannelAllocator.h"

using namespace lmms;

class MpeChannelAllocatorTest : public QObject
{
	Q_OBJECT
private slots:
	void notesGetDistinctMemberChannels()
	{
		MpeChannelAllocator channels;
		int notes[MpeChannelAllocator::MaxMemberChannels];
		std::set<int> used;
		for (auto& note : notes)
		{
			const int channel = channels.allocate(&note);
			QVERIFY(channel >= 1 && channel <= MpeChannelAllocator::MaxMemberChannels);
			QVERIFY(channels.owns(channel, &note));
			used.insert(channel);
		}
		QCOMPARE(int(used.size()), MpeChannelAllocator::MaxMemberChannels);
		QVERIFY(!channels.owns(0, &notes[0]));
	}

	void reusesChannelReleasedLongestAgo()
	{
		MpeChannelAllocator channels{3};
		int a, b, c, d, e;
		const int ca = channels.allocate(&a);
		const int cb = channels.allocate(&b);
		const int cc = channels.allocate(&c);
		channels.release(&b);
		channels.release(&a);
		// b's channel has been free longest, so a's release tail keeps its bend.
		QCOMPARE(channels.allocate(&d), cb);
		QCOMPARE(channels.allocate(&e), ca);
		QVERIFY(channels.owns(cc, &c));
		QVERIFY(!channels.owns(ca, &a));
	}

	void prefersNeverUsedChannels()
	{
		MpeChannelAllocator channels{3};
		int a, b;
		const int first = channels.allocate(&a);
		channels.release(&a);
		QVERIFY(channels.allocate(&b) != first);
	}

	void stealsOldestNoteWhenFull()
	{
		MpeChannelAllocator channels{2};
		int a, b, c, d;
		const int ca = channels.allocate(&a);
		const int cb = channels.allocate(&b);
		QCOMPARE(channels.allocate(&c), ca);
		QVERIFY(!channels.owns(ca, &a));
		QCOMPARE(channels.allocate(&d), cb);
		// The stolen note's release must not free the channel of the new note.
		channels.release(&a);
		QVERIFY(channels.owns(ca, &c));
	}

	void memberCountLimitsChannels()
	{
		MpeChannelAllocator channels{4};
		channels.setMemberCount(1);
		QCOMPARE(channels.memberCount(), 1);
		int a, b;
		QCOMPARE(channels.allocate(&a), 1);
		QCOMPARE(channels.allocate(&b), 1);
		channels.setMemberCount(100);
		QCOMPARE(channels.memberCount(), MpeChannelAllocator::MaxMemberChannels);
		channels.setMemberCount(0);
		QCOMPARE(channels.memberCount(), 1);
	}
};

QTEST_GUILESS_MAIN(MpeChannelAllocatorTest)
#include "MpeChannelAllocatorTest.moc"
