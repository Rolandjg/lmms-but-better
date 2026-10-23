/* Copyright (c) 2026 LMMS developers. SPDX-License-Identifier: GPL-2.0-or-later */
#include <QtTest>
#include <QAction>
#include <memory>

#include "AudioDummy.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "MainApplication.h"
#include "MainWindow.h"
#include "PianoRoll.h"
#include "Song.h"
#include "SongEditor.h"
#include "TimeLineWidget.h"

using namespace lmms;

//! Keyboard shortcuts for the Song Editor's tools, which must only act while the Song Editor has focus
class SongEditorShortcutsTest : public QObject
{
	Q_OBJECT
	QTemporaryDir m_configDir;
	std::unique_ptr<gui::GuiApplication> m_gui;

	gui::SongEditor* songEditor() { return m_gui->songEditor()->m_editor; }
	//! Through the base class, where these are public
	gui::TrackContainerView* container() { return songEditor(); }
	Timeline& timeline() { return Engine::getSong()->getTimeline(Song::PlayMode::Song); }

	void focus(QWidget* widget)
	{
		QApplication::setActiveWindow(m_gui->mainWindow());
		widget->setFocus(Qt::OtherFocusReason);
		QCoreApplication::processEvents();
		QCOMPARE(QApplication::focusWidget(), widget);
	}

	//! Like a real key press: shortcuts get the first go, then the focus widget
	void press(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
	{
		QTest::keyClick(QApplication::focusWidget(), key, modifiers);
		QCoreApplication::processEvents();
	}

	QAction* action(const QString& text)
	{
		for (auto action : m_gui->songEditor()->findChildren<QAction*>())
		{
			if (action->text().startsWith(text)) { return action; }
		}
		return nullptr;
	}

private slots:
	void initTestCase()
	{
		QVERIFY(m_configDir.isValid());
		ConfigManager::inst()->loadConfigFile(m_configDir.path() + "/lmmsrc.xml");
		ConfigManager::inst()->setWorkingDir(m_configDir.path() + "/work/");
		ConfigManager::inst()->createWorkingDir();
		ConfigManager::inst()->setValue("app", "configured", "1");
		ConfigManager::inst()->setValue("audioengine", "audiodev", AudioDummy::name());
		ConfigManager::inst()->setValue("ui", "enableautosave", "0");
		m_gui = std::make_unique<gui::GuiApplication>();
		m_gui->mainWindow()->show();
		m_gui->songEditor()->show();
	}

	void init()
	{
		m_gui->songEditor()->parentWidget()->show();
		focus(songEditor());
		action(tr("Draw mode"))->trigger();
	}

	void editModes()
	{
		press(Qt::Key_K, Qt::ShiftModifier);
		QVERIFY(container()->knifeMode());
		QVERIFY(action(tr("Knife mode"))->isChecked());

		press(Qt::Key_S, Qt::ShiftModifier);
		QVERIFY(!container()->knifeMode());
		QVERIFY(container()->allowRubberband());
		QVERIFY(action(tr("Edit mode"))->isChecked());

		press(Qt::Key_D, Qt::ShiftModifier);
		QVERIFY(!container()->knifeMode());
		QVERIFY(!container()->allowRubberband());
		QVERIFY(action(tr("Draw mode"))->isChecked());
	}

	void proportionalSnap()
	{
		const auto snap = action(tr("Toggle proportional snap"));
		QVERIFY(!snap->isChecked());
		QSignalSpy spy(songEditor(), &gui::SongEditor::proportionalSnapChanged);
		press(Qt::Key_P, Qt::ShiftModifier);
		QVERIFY(snap->isChecked());
		QCOMPARE(spy.count(), 1);
		press(Qt::Key_P, Qt::ShiftModifier);
		QVERIFY(!snap->isChecked());
		QCOMPARE(spy.count(), 2);
	}

	void loopPoints()
	{
		const bool wasEnabled = timeline().loopEnabled();
		press(Qt::Key_L, Qt::ShiftModifier);
		QCOMPARE(timeline().loopEnabled(), !wasEnabled);
		press(Qt::Key_L, Qt::ShiftModifier);
		QCOMPARE(timeline().loopEnabled(), wasEnabled);
	}

	void zoom()
	{
		// The bar width at startup is rounded differently from the one the zoom slider gives,
		// so start from one the slider gave
		press(Qt::Key_Minus, Qt::ControlModifier);
		const float zoomedOut = container()->pixelsPerBar();
		press(Qt::Key_Equal, Qt::ControlModifier);
		const float start = container()->pixelsPerBar();
		QVERIFY(start > zoomedOut);
		press(Qt::Key_Equal, Qt::ControlModifier);
		QVERIFY(container()->pixelsPerBar() > start);
		press(Qt::Key_Minus, Qt::ControlModifier);
		QCOMPARE(container()->pixelsPerBar(), start);
	}

	void addMarker()
	{
		const auto markers = timeline().markers().size();
		press(Qt::Key_M);
		QCOMPARE(timeline().markers().size(), markers + 1);
	}

	//! The Piano Roll uses the same keys for its own tools; those must not reach the Song Editor
	void otherEditorsKeepTheirKeys()
	{
		auto pianoRoll = m_gui->pianoRoll();
		pianoRoll->parentWidget()->show();
		pianoRoll->show();
		focus(pianoRoll->findChild<gui::PianoRoll*>());

		const bool loop = timeline().loopEnabled();
		press(Qt::Key_K, Qt::ShiftModifier);
		press(Qt::Key_S, Qt::ShiftModifier);
		press(Qt::Key_L, Qt::ShiftModifier);
		press(Qt::Key_P, Qt::ShiftModifier);
		QVERIFY(action(tr("Draw mode"))->isChecked());
		QVERIFY(!action(tr("Toggle proportional snap"))->isChecked());
		QCOMPARE(timeline().loopEnabled(), loop);

		// Two shortcuts claiming the same key would leave both doing nothing
		QAction* pianoRollSelect = nullptr;
		for (auto action : pianoRoll->findChildren<QAction*>())
		{
			if (action->text().startsWith(tr("Select mode"))) { pianoRollSelect = action; }
		}
		QVERIFY(pianoRollSelect);
		QVERIFY(pianoRollSelect->isChecked());

		// and the Song Editor's keys still work once it has focus again
		focus(songEditor());
		press(Qt::Key_K, Qt::ShiftModifier);
		QVERIFY(container()->knifeMode());
	}
};

int main(int argc, char* argv[])
{
	gui::MainApplication app{argc, argv};
	SongEditorShortcutsTest test;
	return QTest::qExec(&test, argc, argv);
}
#include "SongEditorShortcutsTest.moc"
