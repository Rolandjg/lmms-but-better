/* Copyright (c) 2026 LMMS developers. SPDX-License-Identifier: GPL-2.0-or-later */
#include <QtTest>
#include <memory>

#include "AudioDummy.h"
#include "AudioEngine.h"
#include "CommandPalette.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "InstrumentTrack.h"
#include "MainApplication.h"
#include "MainWindow.h"
#include "Song.h"

using namespace lmms;

class CommandPaletteTest : public QObject
{
	Q_OBJECT
	QTemporaryDir m_configDir;
	std::unique_ptr<gui::GuiApplication> m_gui;

	gui::CommandPalette* palette()
	{
		return m_gui->mainWindow()->findChild<gui::CommandPalette*>();
	}

	int rowOf(gui::CommandPalette* p, const QString& title)
	{
		for (int i = 0; i < p->visibleCount(); ++i)
		{
			if (p->visibleTitle(i) == title) { return i; }
		}
		return -1;
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
		m_gui->mainWindow()->resize(1280, 800);
		m_gui->mainWindow()->show();
	}

	void fuzzyMatching()
	{
		using gui::fuzzyMatchScore;
		QVERIFY(fuzzyMatchScore("quant", "Quantize") > 0);
		QVERIFY(fuzzyMatchScore("qtz", "Quantize") > 0);
		QCOMPARE(fuzzyMatchScore("xyz", "Quantize"), -1);
		// Words may come in any order
		QVERIFY(fuzzyMatchScore("reverb add", "Add effect: ReverbSC") > 0);
		// Word starts and contiguous runs beat scattered letters
		QVERIFY(fuzzyMatchScore("se", "Song Editor") > fuzzyMatchScore("se", "Pause"));
		QVERIFY(fuzzyMatchScore("mixer", "Mixer") > fuzzyMatchScore("mixer", "Go to mixer channel 3: Drums"));

		QVector<int> positions;
		QVERIFY(fuzzyMatchScore("pr", "Piano Roll", &positions) > 0);
		QCOMPARE(positions, (QVector<int>{0, 6}));
	}

	void collectsActionsFromEverywhere()
	{
		auto p = palette();
		QVERIFY(p);
		p->popup();
		QVERIFY(p->isVisible());

		QStringList titles;
		for (const auto& command : p->commands()) { titles << command.title; }
		QVERIFY(titles.contains("Save As..."));
		QVERIFY(titles.contains("Quantize"));              // piano roll toolbar
		QVERIFY(titles.contains("Draw mode"));             // shortcut suffix stripped
		QVERIFY(titles.contains("Add instrument: TripleOscillator"));
		QVERIFY(titles.contains("Play / pause song"));

		p->setQuery("triple");
		QVERIFY(p->visibleCount() > 0);
		QCOMPARE(p->visibleTitle(0), QString("Add instrument: TripleOscillator"));

		if (const QByteArray shot = qgetenv("LMMS_PALETTE_SNAPSHOT"); !shot.isEmpty())
		{
			p->popup();
			p->setQuery("pi ro");
			QCoreApplication::processEvents();
			m_gui->mainWindow()->grab().save(QString::fromLocal8Bit(shot));
		}
		p->hide();
	}

	void tempoQuery()
	{
		auto p = palette();
		p->popup();
		p->setQuery("tempo 137");
		QCOMPARE(p->visibleTitle(0), QString("Set tempo to 137 BPM"));
		p->runRow(0);
		QVERIFY(p->isHidden());
		QTRY_COMPARE(Engine::getSong()->getTempo(), 137);

		p->popup();
		p->setQuery("90 bpm");
		QCOMPARE(p->visibleTitle(0), QString("Set tempo to 90 BPM"));
		p->hide();
	}

	void addInstrumentTrack()
	{
		auto p = palette();
		const auto before = Engine::getSong()->tracks().size();
		p->popup();
		p->setQuery("add instrument kicker");
		const int row = rowOf(p, "Add instrument: Kicker");
		QVERIFY(row >= 0);
		p->runRow(row);
		QTRY_COMPARE(Engine::getSong()->tracks().size(), before + 1);
		auto track = dynamic_cast<InstrumentTrack*>(Engine::getSong()->tracks().back());
		QVERIFY(track);
		QCOMPARE(track->instrumentName(), QString("Kicker"));

		// Recently used commands come first on an empty query
		p->popup();
		QCOMPARE(p->visibleTitle(0), QString("Add instrument: Kicker"));
		p->hide();
	}

	void markers()
	{
		auto song = Engine::getSong();
		auto& timeline = song->getTimeline(Song::PlayMode::Song);
		timeline.clearMarkers();
		timeline.addMarker(TimePos{4 * TimePos::ticksPerBar()}, "Chorus", QColor("#e0544e"));

		auto p = palette();
		p->popup();
		QStringList titles;
		for (const auto& command : p->commands()) { titles << command.title; }
		QVERIFY(titles.contains("Add marker at playhead"));
		QVERIFY(titles.contains("Jump to next marker"));

		p->setQuery("chorus");
		const int row = rowOf(p, "Go to marker: Chorus");
		QVERIFY(row >= 0);
		p->runRow(row);
		QTRY_COMPARE(static_cast<int>(timeline.pos()), 4 * TimePos::ticksPerBar());

		// Adding at the playhead and jumping between markers
		p->popup();
		p->setQuery("jump previous marker");
		const int previous = rowOf(p, "Jump to previous marker");
		QVERIFY(previous >= 0);
		p->runRow(previous);
		QTRY_COMPARE(static_cast<int>(timeline.pos()), 0);

		p->popup();
		p->setQuery("add marker");
		const int add = rowOf(p, "Add marker at playhead");
		QVERIFY(add >= 0);
		p->runRow(add);
		QTRY_COMPARE(timeline.markers().size(), std::size_t{2});
		QCOMPARE(static_cast<int>(timeline.markers()[0].pos), 0);
		timeline.clearMarkers();
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
	CommandPaletteTest test;
	return QTest::qExec(&test, argc, argv);
}
#include "CommandPaletteTest.moc"
