/* Copyright (c) 2026 LMMS developers. SPDX-License-Identifier: GPL-2.0-or-later */
#include <QtTest>
#include <QPlainTextEdit>
#include <QProcess>
#include <memory>

#include "AudioDummy.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "InstrumentTrack.h"
#include "MainApplication.h"
#include "MainWindow.h"
#include "MidiClip.h"
#include "NotePlayHandle.h"
#include "ScriptConsole.h"
#include "ScriptRunner.h"
#include "ScriptServer.h"
#include "Song.h"

using namespace lmms;

class ScriptConsoleTest : public QObject
{
	Q_OBJECT
	QTemporaryDir m_configDir;
	std::unique_ptr<gui::GuiApplication> m_gui;

	gui::ScriptConsole* console() { return m_gui->mainWindow()->scriptConsole(); }

	QString output() { return console()->findChild<QPlainTextEdit*>("scriptConsoleOutput")->toPlainText(); }

	//! Run code in the console interpreter and wait until it finished
	bool run(const QString& code)
	{
		auto runner = console()->findChild<scripting::ScriptRunner*>();
		QSignalSpy done(runner, &scripting::ScriptRunner::executionFinished);
		console()->runCode(code, true);
		return done.wait(30000) && done.first().first().toBool();
	}

private slots:
	void initTestCase()
	{
		QProcess probe;
		probe.start(scripting::ScriptRunner::pythonExecutable(), {"-c", "import sys; assert sys.version_info >= (3, 6)"});
		if (!probe.waitForFinished(10000) || probe.exitCode() != 0) { QSKIP("Python 3 is not available"); }

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
		Engine::getSong()->createEmptyProject();
	}

	void findsTheBundledModule()
	{
		QVERIFY(QFile::exists(scripting::ScriptRunner::pythonModuleDir() + "/lmms/__init__.py"));
		QVERIFY(QDir(scripting::ScriptRunner::examplesDir()).entryList({"*.py"}).size() > 0);
	}

	void runsCodeAgainstTheSong()
	{
		QVERIFY(run("t = song.add_instrument_track('tripleoscillator', name='From Python')\n"
			"c = t.add_midi_clip(0, 4)\n"
			"c.add_notes([(p, i, 1) for i, p in enumerate(lmms.chord('C4', 'maj7'))])\n"
			"print('notes:', len(c.notes))"));
		QVERIFY(output().contains("notes: 4"));

		const auto& tracks = Engine::getSong()->tracks();
		QCOMPARE(tracks.size(), std::size_t{1});
		QCOMPARE(tracks[0]->name(), QString("From Python"));
		auto clip = dynamic_cast<MidiClip*>(tracks[0]->getClips().at(0));
		QVERIFY(clip);
		QCOMPARE(clip->notes().size(), std::size_t{4});
		QCOMPARE(clip->notes()[3]->key(), 71);
	}

	void keepsStateBetweenRunsAndPrintsExpressions()
	{
		QVERIFY(run("answer = 6 * 7"));
		QVERIFY(run("answer"));
		QVERIFY(output().contains("42"));
	}

	void reportsErrors()
	{
		QVERIFY(!run("song.add_instrument_track('does-not-exist')"));
		QVERIFY(output().contains("LmmsError: no instrument plugin \"does-not-exist\""));
		// a track whose setup fails is removed again
		const auto before = Engine::getSong()->tracks().size();
		QVERIFY(!run("song.add_instrument_track('tripleoscillator', params={'no-such-knob': 1})"));
		QVERIFY(output().contains("no parameter \"no-such-knob\""));
		QTest::qWait(10);
		QCOMPARE(Engine::getSong()->tracks().size(), before);
		// the interpreter survives errors
		QVERIFY(run("print('still alive')"));
		QVERIFY(output().contains("still alive"));
	}

	void screenshot()
	{
		if (const auto shot = qgetenv("LMMS_SCRIPT_CONSOLE_SNAPSHOT"); !shot.isEmpty())
		{
			m_gui->mainWindow()->toggleScriptConsole();
			QTest::qWait(50);
			console()->parentWidget()->grab().save(QString::fromLocal8Bit(shot));
		}
	}

	void cleanupTestCase()
	{
		if (!m_gui) { return; }
		console()->stop();
		scripting::ScriptServer::instance()->stop();
		m_gui->mainWindow()->deleteLater();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		m_gui.reset();
		NotePlayHandleManager::free();
	}
};

int main(int argc, char** argv)
{
	gui::MainApplication app{argc, argv};
	ScriptConsoleTest test;
	return QTest::qExec(&test, argc, argv);
}
#include "ScriptConsoleTest.moc"
