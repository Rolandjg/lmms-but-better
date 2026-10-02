/* Copyright (c) 2026 LMMS developers. SPDX-License-Identifier: GPL-2.0-or-later */
#include <QtTest>
#include <QMouseEvent>
#include <algorithm>
#include <memory>

#include "AudioDummy.h"
#include "AudioEngine.h"
#include "ConfigManager.h"
#include "DetuningHelper.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "InstrumentTrack.h"
#include "MainApplication.h"
#include "MainWindow.h"
#include "MidiClip.h"
#include "PianoRoll.h"

using namespace lmms;

class PianoRollRenderingTest : public QObject
{
	Q_OBJECT
	QTemporaryDir m_configDir;
	std::unique_ptr<gui::GuiApplication> m_gui;
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
		m_gui->mainWindow()->show();
	}

	void bendsStayAboveNotes_data()
	{
		QTest::addColumn<int>("progression");
		QTest::newRow("discrete") << static_cast<int>(AutomationClip::ProgressionType::Discrete);
		QTest::newRow("linear") << static_cast<int>(AutomationClip::ProgressionType::Linear);
		QTest::newRow("cubic") << static_cast<int>(AutomationClip::ProgressionType::CubicHermite);
	}

	void bendsStayAboveNotes()
	{
		QFETCH(int, progression);
		auto guard = Engine::audioEngine()->requestChangesGuard();
		struct TestContainer : TrackContainer
		{
			QString nodeName() const override { return "trackcontainer"; }
		} container;
		InstrumentTrack track{&container};
		MidiClip clip{&track};
		clip.clearNotes();
		auto* bent = clip.addNote(Note{TimePos{384}, TimePos{0}, 60}, false);
		bent->createDetuning();
		auto* curve = bent->detuning()->automationClip();
		curve->putValue(0, 0, false);
		curve->putValue(96, 12, false);
		curve->putValue(384, 12, false);
		curve->setProgressionType(static_cast<AutomationClip::ProgressionType>(progression));
		// The bend crosses this note body and then runs along its center.
		clip.addNote(Note{TimePos{384}, TimePos{0}, 72}, false);
		auto* window = m_gui->pianoRoll();
		window->setCurrentMidiClip(&clip);
		window->show();
		QCoreApplication::processEvents();
		auto* roll = window->findChild<gui::PianoRoll*>();
		QVERIFY(roll);
		const QImage before = roll->grab().toImage();
		QVERIFY(!before.isNull());

		// Alter only draw order, keeping note positions and the viewport identical.
		// The underlying vector belongs to this mutable fixture.
		auto& notes = const_cast<NoteVector&>(clip.notes());
		std::reverse(notes.begin(), notes.end());
		roll->update();
		const QImage after = roll->grab().toImage();
		window->setCurrentMidiClip(nullptr);
		QVERIFY2(before == after, "A later note body must not erase an earlier pitch bend");
	}

	void mouseHoverRepaints_data()
	{
		QTest::addColumn<bool>("pitchBendMode");
		QTest::newRow("draw") << false;
		QTest::newRow("pitch-bend") << true;
	}

	void mouseHoverRepaints()
	{
		QFETCH(bool, pitchBendMode);
		auto guard = Engine::audioEngine()->requestChangesGuard();
		struct TestContainer : TrackContainer
		{
			QString nodeName() const override { return "trackcontainer"; }
		} container;
		InstrumentTrack track{&container};
		MidiClip clip{&track};
		clip.clearNotes();
		auto* note = clip.addNote(Note{TimePos{384}, TimePos{0}, 60}, false);
		note->createDetuning();
		note->detuning()->automationClip()->putValue(0, 0, false);
		note->detuning()->automationClip()->putValue(384, 12, false);
		note->setSelected(true);
		auto* window = m_gui->pianoRoll();
		window->setCurrentMidiClip(&clip);
		window->show();
		auto* roll = window->findChild<gui::PianoRoll*>();
		QVERIFY(roll);
		QVERIFY(QMetaObject::invokeMethod(roll, "setEditMode", Qt::DirectConnection,
			Q_ARG(int, static_cast<int>(pitchBendMode
				? gui::PianoRoll::EditMode::Detuning : gui::PianoRoll::EditMode::Draw))));
		roll->setFocus();
		QTest::qWait(50);
		QVERIFY(roll->isVisible());

		struct PaintObserver : QObject
		{
			int paints = 0;
			bool eventFilter(QObject*, QEvent* event) override
			{
				if (event->type() == QEvent::Paint) { ++paints; }
				return false;
			}
		} observer;
		roll->installEventFilter(&observer);
		// Deliver movement with no buttons held, without note on/off signals
		// or an explicit grab/update that could hide a missing repaint request.
		const QPointF pos(roll->width() / 2, roll->height() / 2);
		QMouseEvent move(QEvent::MouseMove, pos, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
		QApplication::sendEvent(roll, &move);
		QTest::qWait(50);
		roll->removeEventFilter(&observer);
		window->setCurrentMidiClip(nullptr);
		QVERIFY2(observer.paints > 0, "Mouse movement must repaint the hovered row in every edit mode");
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
	PianoRollRenderingTest test;
	return QTest::qExec(&test, argc, argv);
}
#include "PianoRollRenderingTest.moc"
