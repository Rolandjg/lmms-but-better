/* Copyright (c) 2026 LMMS developers. SPDX-License-Identifier: GPL-2.0-or-later */
#include <QtTest>
#include <memory>

#include "AudioDummy.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "InstrumentTrack.h"
#include "MainApplication.h"
#include "MainWindow.h"
#include "MidiCapture.h"
#include "MidiClip.h"
#include "Note.h"
#include "Song.h"

using namespace lmms;
using gui::MidiCapture;

class MidiCaptureTest : public QObject
{
	Q_OBJECT
	QTemporaryDir m_configDir;
	std::unique_ptr<gui::GuiApplication> m_gui;

	static MidiCapture::CapturedNote note(int key, qint64 start, qint64 end, int session = 0,
		double startTicks = -1, double endTicks = -1)
	{
		MidiCapture::CapturedNote n;
		n.key = key;
		n.startMs = start;
		n.endMs = end;
		n.session = session;
		n.startTicks = startTicks;
		n.endTicks = endTicks;
		return n;
	}

private slots:
	void initTestCase()
	{
		NotePlayHandleManager::init();
		QVERIFY(m_configDir.isValid());
		ConfigManager::inst()->loadConfigFile(m_configDir.path() + "/lmmsrc.xml");
		ConfigManager::inst()->setWorkingDir(m_configDir.path() + "/work/");
		ConfigManager::inst()->createWorkingDir();
		ConfigManager::inst()->setValue("app", "configured", "1");
		ConfigManager::inst()->setValue("audioengine", "audiodev", AudioDummy::name());
		ConfigManager::inst()->setValue("ui", "enableautosave", "0");
		m_gui = std::make_unique<gui::GuiApplication>();
	}

	void takeSplitsOnLongPauses()
	{
		// Two phrases 10 s apart: only the latest one is the take
		const std::vector<MidiCapture::CapturedNote> notes{
			note(60, 0, 400), note(62, 500, 900),
			note(64, 10900, 11400), note(65, 11500, 12000), note(67, 12000, 12500)};
		const double ticksPerMs = 120 * 48 / 60000.0; // 120 BPM
		const auto take = MidiCapture::extractTake(notes, 13000, ticksPerMs);
		QCOMPARE(take.notes.size(), std::size_t{3});
		QCOMPARE(take.firstIndex, std::size_t{2});
		QVERIFY(!take.fromPlayback);
		QCOMPARE(take.notes[0].key, 64);
		QCOMPARE(take.notes[0].pos, 0.0);
		// 600 ms at 120 BPM is 1.2 beats = 57.6 ticks
		QVERIFY(std::abs(take.notes[1].pos - 57.6) < 0.01);
		QVERIFY(std::abs(take.notes[0].length - 48.0) < 0.01);
	}

	void heldNotesBridgeGaps()
	{
		// A pad held for 8 s keeps the phrase together even though note-ons are far apart
		const std::vector<MidiCapture::CapturedNote> notes{note(48, 0, 8000), note(60, 7000, 7500)};
		const auto take = MidiCapture::extractTake(notes, 9000, 0.1);
		QCOMPARE(take.notes.size(), std::size_t{2});
		// Still-held notes end "now"
		const std::vector<MidiCapture::CapturedNote> held{note(50, 1000, -1)};
		QCOMPARE(MidiCapture::extractTake(held, 3000, 0.1).notes[0].length, 200.0);
	}

	void playbackTakesKeepSongPositions()
	{
		const std::vector<MidiCapture::CapturedNote> notes{
			note(60, 0, 100, 1, 400, 420),
			note(62, 200, 300, 1, 440, 460),
			// loop wrapped back to 384: a new pass starts
			note(64, 900, 1000, 1, 390, 410),
			note(65, 1100, 1200, 1, 430, -1)};
		const auto take = MidiCapture::extractTake(notes, 2000, 0.1);
		QVERIFY(take.fromPlayback);
		QCOMPARE(take.notes.size(), std::size_t{2});
		QCOMPARE(take.startTicks, 390.0);
		QCOMPARE(take.notes[1].pos, 430.0);
		// No end position recorded: fall back to wall clock time
		QCOMPARE(take.notes[1].length, 10.0);
	}

	void sessionsSeparateTakes()
	{
		const std::vector<MidiCapture::CapturedNote> notes{note(60, 0, 100, 0), note(62, 200, 300, 1)};
		QCOMPARE(MidiCapture::extractTake(notes, 400, 0.1).notes.size(), std::size_t{1});
	}

	void captureCreatesClip()
	{
		auto mainWindow = m_gui->mainWindow();
		auto capture = mainWindow->findChild<MidiCapture*>();
		QVERIFY(capture);
		QVERIFY(!capture->capture());

		auto track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(track);
		QSignalSpy available(capture, &MidiCapture::availabilityChanged);
		for (int key : {60, 64, 67})
		{
			capture->notePressed(track, Note(TimePos(0), TimePos(0), key, 90));
		}
		QCOMPARE(available.count(), 1);
		QVERIFY(capture->hasCapture());
		QTest::qWait(50);
		for (int key : {60, 64, 67})
		{
			capture->noteReleased(track, Note(TimePos(0), TimePos(0), key));
		}

		const auto clipsBefore = track->getClips().size();
		QString message;
		auto clip = capture->capture(&message);
		QVERIFY(clip);
		QVERIFY(message.contains("3"));
		QCOMPARE(track->getClips().size(), clipsBefore + 1);
		QCOMPARE(clip->notes().size(), std::size_t{3});
		QCOMPARE(clip->notes()[0]->getVolume(), volume_t{90});
		QCOMPARE(clip->startPosition().getTicks() % TimePos::ticksPerBar(), 0);
		// The take is consumed
		QVERIFY(!capture->hasCapture());
	}

	void cleanupTestCase()
	{
		m_gui->mainWindow()->deleteLater();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		m_gui.reset();
		NotePlayHandleManager::free();
	}
};

int main(int argc, char** argv)
{
	gui::MainApplication app{argc, argv};
	MidiCaptureTest test;
	return QTest::qExec(&test, argc, argv);
}
#include "MidiCaptureTest.moc"
