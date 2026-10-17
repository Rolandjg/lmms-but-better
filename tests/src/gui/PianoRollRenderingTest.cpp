/* Copyright (c) 2026 LMMS developers. SPDX-License-Identifier: GPL-2.0-or-later */
#include <QtTest>
#include <QMouseEvent>
#include <algorithm>
#include <memory>

#include <QAction>
#include <QDialog>
#include <QSlider>
#include <QTimer>
#include <QToolButton>

#include "AudioDummy.h"
#include "AudioEngine.h"
#include "ComboBox.h"
#include "ConfigManager.h"
#include "DetuningHelper.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "InstrumentTrack.h"
#include "MainApplication.h"
#include "MainWindow.h"
#include "MidiClip.h"
#include "PianoRoll.h"
#include "ProjectJournal.h"

using namespace lmms;

class PianoRollRenderingTest : public QObject
{
	Q_OBJECT
	QTemporaryDir m_configDir;
	std::unique_ptr<gui::GuiApplication> m_gui;

	// Keeps the fixtures out of the Song Editor's asynchronous track views
	struct TestContainer : TrackContainer
	{
		QString nodeName() const override { return "trackcontainer"; }
	};

	gui::PianoRoll* openInPitchBendMode(MidiClip& clip)
	{
		auto* window = m_gui->pianoRoll();
		window->setCurrentMidiClip(&clip);
		window->show();
		auto* roll = window->findChild<gui::PianoRoll*>();
		QMetaObject::invokeMethod(roll, "setEditMode", Qt::DirectConnection,
			Q_ARG(int, static_cast<int>(gui::PianoRoll::EditMode::Detuning)));
		QCoreApplication::processEvents();
		return roll;
	}

	static void send(gui::PianoRoll* roll, QEvent::Type type, QPoint pos, Qt::MouseButton button, Qt::MouseButtons buttons)
	{
		QCursor::setPos(roll->mapToGlobal(pos));
		QMouseEvent event(type, pos, roll->mapToGlobal(pos), button, buttons, Qt::NoModifier);
		QApplication::sendEvent(roll, &event);
	}

	static void click(gui::PianoRoll* roll, QPoint pos, Qt::MouseButton button)
	{
		send(roll, QEvent::MouseMove, pos, Qt::NoButton, Qt::NoButton);
		send(roll, QEvent::MouseButtonPress, pos, button, button);
		send(roll, QEvent::MouseButtonRelease, pos, button, Qt::NoButton);
	}

	static void drag(gui::PianoRoll* roll, QPoint from, QPoint to)
	{
		send(roll, QEvent::MouseMove, from, Qt::NoButton, Qt::NoButton);
		send(roll, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
		send(roll, QEvent::MouseMove, (from + to) / 2, Qt::NoButton, Qt::LeftButton);
		send(roll, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
		send(roll, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
	}

	//! Hovers the whole editor and returns the centers of the spots showing @p shape, left to right
	static std::vector<QPoint> hits(gui::PianoRoll* roll, Qt::CursorShape shape)
	{
		std::vector<std::vector<QPoint>> clusters;
		for (int y = 0; y < roll->height(); y += 2)
		{
			for (int x = 0; x < roll->width(); x += 2)
			{
				QMouseEvent move(QEvent::MouseMove, QPoint(x, y), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
				QApplication::sendEvent(roll, &move);
				if (roll->cursor().shape() != shape) { continue; }
				auto cluster = std::find_if(clusters.begin(), clusters.end(), [&](const auto& points)
				{
					return (points.front() - QPoint(x, y)).manhattanLength() < 24;
				});
				if (cluster == clusters.end()) { clusters.push_back({QPoint(x, y)}); }
				else { cluster->push_back(QPoint(x, y)); }
			}
		}
		std::vector<QPoint> centers;
		for (const auto& points : clusters)
		{
			QPoint sum;
			for (const auto& point : points) { sum += point; }
			centers.push_back(sum / static_cast<int>(points.size()));
		}
		std::sort(centers.begin(), centers.end(), [](QPoint a, QPoint b) { return a.x() < b.x(); });
		return centers;
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

	void defaultsOnFreshConfig()
	{
		auto* window = m_gui->pianoRoll();
		QString quantization, zoomY;
		for (auto* combo : window->findChildren<gui::ComboBox*>())
		{
			auto* model = combo->model();
			if (!model) { continue; }
			if (combo->toolTip() == "Quantization") { quantization = model->currentText(); }
			if (combo->toolTip() == "Vertical zooming") { zoomY = model->currentText(); }
		}
		QCOMPARE(quantization, QString("1/16"));
		QCOMPARE(zoomY, QString("100%"));
	}

	void grabOffGridPoint()
	{
		auto guard = Engine::audioEngine()->requestChangesGuard();
		TestContainer container;
		InstrumentTrack track{&container};
		MidiClip clip{&track};
		clip.clearNotes();
		auto* note = clip.addNote(Note{TimePos{192}, TimePos{0}, 60}, false);
		note->createDetuning();
		auto* curve = note->detuning()->automationClip();
		curve->putValue(0, 0, false);
		curve->putValue(50, 4, false); // off the 1/16 grid
		note->setSelected(true);
		auto* roll = openInPitchBendMode(clip);

		const auto points = hits(roll, Qt::SizeAllCursor);
		QCOMPARE(points.size(), 2);
		const QPoint start = points[0], offGrid = points[1];
		const int keyHeight = (start.y() - offGrid.y()) / 4;
		QVERIFY(keyHeight > 2);

		// Dragging straight up keeps the point's exact time
		drag(roll, offGrid, offGrid - QPoint(0, keyHeight));
		curve = clip.notes().front()->detuning()->automationClip();
		QVERIFY(curve->getTimeMap().contains(50));
		QCOMPARE(curve->getTimeMap().size(), 2);
		QCOMPARE(curve->getTimeMap()[50].getInValue(), 5.f);

		// Dragging sideways snaps it to the grid
		drag(roll, offGrid - QPoint(0, keyHeight), offGrid + QPoint(30, -keyHeight));
		QCOMPARE(curve->getTimeMap().size(), 2);
		QVERIFY(!curve->getTimeMap().contains(50));
		QCOMPARE(curve->getTimeMap().lastKey() % 12, 0);
		gui::getGUI()->pianoRoll()->setCurrentMidiClip(nullptr);
	}

	void steepHandleStaysReachable()
	{
		auto guard = Engine::audioEngine()->requestChangesGuard();
		TestContainer container;
		InstrumentTrack track{&container};
		MidiClip clip{&track};
		clip.clearNotes();
		auto* note = clip.addNote(Note{TimePos{192}, TimePos{0}, 60}, false);
		note->createDetuning();
		auto* curve = note->detuning()->automationClip();
		curve->putValue(0, 0, false);
		curve->putValue(96, 5, false);
		// Several semitones per tick: the old handles were drawn hundreds of pixels away
		QVERIFY(curve->setNodeTangent(96, false, -3.f, false));
		note->setSelected(true);
		auto* roll = openInPitchBendMode(clip);

		const auto points = hits(roll, Qt::SizeAllCursor);
		const auto handles = hits(roll, Qt::CrossCursor);
		QCOMPARE(points.size(), 2);
		QCOMPARE(handles.size(), 2);
		for (const QPoint& handle : handles)
		{
			const int distance = std::min((handle - points[0]).manhattanLength(), (handle - points[1]).manhattanLength());
			QVERIFY2(distance < 50, "Tangent handles stay next to their point");
		}

		// Right-clicking the handle restores the automatic tangent
		const QPoint inHandle = (handles[0] - points[1]).manhattanLength() < (handles[1] - points[1]).manhattanLength()
			? handles[0] : handles[1];
		click(roll, inHandle, Qt::RightButton);
		QVERIFY(!clip.notes().front()->detuning()->automationClip()->getTimeMap()[96].lockedTangents());
		gui::getGUI()->pianoRoll()->setCurrentMidiClip(nullptr);
	}

	void velocityLaneWorksInPitchBendMode()
	{
		auto guard = Engine::audioEngine()->requestChangesGuard();
		TestContainer container;
		InstrumentTrack track{&container};
		MidiClip clip{&track};
		clip.clearNotes();
		auto* note = clip.addNote(Note{TimePos{192}, TimePos{0}, 60}, false);
		note->createDetuning();
		note->detuning()->automationClip()->putValue(0, 0, false);
		note->detuning()->automationClip()->putValue(96, 2, false);
		note->setSelected(true);
		auto* roll = openInPitchBendMode(clip);
		const auto points = hits(roll, Qt::SizeAllCursor);
		QVERIFY(!points.empty());
		const QPoint start = points.front();
		click(roll, QPoint(start.x() + 1, roll->height() - 20), Qt::LeftButton);
		QVERIFY(clip.notes().front()->getVolume() < DefaultVolume);
		gui::getGUI()->pianoRoll()->setCurrentMidiClip(nullptr);
	}

	void duplicateFollowsSelection_data()
	{
		QTest::addColumn<int>("firstPos");
		QTest::addColumn<int>("lastEnd");
		QTest::addColumn<int>("shift");
		QTest::newRow("two beats") << 0 << 96 << 96;
		QTest::newRow("ends off the grid") << 0 << 40 << 48;
		QTest::newRow("pickup from beat 2") << 48 << 192 << 192;
		QTest::newRow("bar and a bit") << 0 << 200 << 384;
	}

	void duplicateFollowsSelection()
	{
		QFETCH(int, firstPos);
		QFETCH(int, lastEnd);
		QFETCH(int, shift);
		auto guard = Engine::audioEngine()->requestChangesGuard();
		TestContainer container;
		InstrumentTrack track{&container};
		MidiClip clip{&track};
		clip.clearNotes();
		auto* first = clip.addNote(Note{TimePos{12}, TimePos{firstPos}, 60}, false);
		first->createDetuning();
		first->detuning()->automationClip()->putValue(0, 0, false);
		first->detuning()->automationClip()->putValue(6, 3, false);
		auto* last = clip.addNote(Note{TimePos{12}, TimePos{lastEnd - 12}, 64}, false);
		first->setSelected(true);
		last->setSelected(true);
		auto* window = m_gui->pianoRoll();
		window->setCurrentMidiClip(&clip);
		auto* roll = window->findChild<gui::PianoRoll*>();
		QVERIFY(QMetaObject::invokeMethod(roll, "duplicateSelectedNotes", Qt::DirectConnection));
		QCOMPARE(clip.notes().size(), 4u);
		std::vector<int> selectedPositions;
		for (const Note* note : clip.notes())
		{
			if (note->selected()) { selectedPositions.push_back(note->pos()); }
		}
		QCOMPARE(selectedPositions, (std::vector<int>{firstPos + shift, lastEnd - 12 + shift}));

		// The copy's glide is its own
		Note* copy = nullptr;
		for (Note* note : clip.notes()) { if (note->selected() && note->key() == 60) { copy = note; } }
		QVERIFY(copy && copy->detuning());
		copy->detuning()->automationClip()->putValue(6, 7, false);
		for (Note* note : clip.notes())
		{
			if (!note->selected() && note->key() == 60)
			{
				QCOMPARE(note->detuning()->automationClip()->valueAt(6), 3.f);
			}
		}
		window->setCurrentMidiClip(nullptr);
	}

	void humanizePreviewsAndUndoes()
	{
		auto guard = Engine::audioEngine()->requestChangesGuard();
		TestContainer container;
		InstrumentTrack track{&container};
		MidiClip clip{&track};
		clip.clearNotes();
		for (int i = 0; i < 16; ++i) { clip.addNote(Note{TimePos{24}, TimePos{i * 48}, 60}, false); }
		auto* window = m_gui->pianoRoll();
		window->setCurrentMidiClip(&clip);
		auto* roll = window->findChild<gui::PianoRoll*>();
		auto unchanged = [&clip]
		{
			int i = 0;
			for (const Note* note : clip.notes())
			{
				if (note->pos() != i * 48 || note->getVolume() != DefaultVolume || note->length() != 24) { return false; }
				++i;
			}
			return true;
		};

		auto runDialog = [&](bool accept)
		{
			bool previewed = false;
			QTimer::singleShot(0, [&]
			{
				auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
				QVERIFY(dialog && dialog->objectName() == "pianoRollHumanizeDialog");
				dialog->findChild<QSlider*>("velocity")->setValue(30);
				dialog->findChild<QSlider*>("timing")->setValue(60);
				dialog->findChild<QSlider*>("length")->setValue(40);
				previewed = !unchanged();
				accept ? dialog->accept() : dialog->reject();
			});
			QVERIFY(QMetaObject::invokeMethod(roll, "humanizeNotes", Qt::DirectConnection));
			QVERIFY2(previewed, "Moving the sliders previews the result");
		};

		runDialog(false);
		QVERIFY2(unchanged(), "Cancel restores the notes");
		runDialog(true);
		QVERIFY(!unchanged());
		QVERIFY(std::is_sorted(clip.notes().begin(), clip.notes().end(), Note::lessThan));
		Engine::projectJournal()->undo();
		QVERIFY2(unchanged(), "Humanizing is one undo step");
		window->setCurrentMidiClip(nullptr);
	}

	void glideMenuSetsCurveShape()
	{
		auto guard = Engine::audioEngine()->requestChangesGuard();
		TestContainer container;
		InstrumentTrack track{&container};
		MidiClip clip{&track};
		clip.clearNotes();
		auto* note = clip.addNote(Note{TimePos{192}, TimePos{0}, 60}, false);
		note->createDetuning();
		note->detuning()->automationClip()->putValue(0, 0, false);
		note->detuning()->automationClip()->putValue(96, 2, false);
		note->setSelected(true);
		auto* window = m_gui->pianoRoll();
		window->setCurrentMidiClip(&clip);
		auto* button = window->findChild<QToolButton*>("pianoRollGlideMenuButton");
		QVERIFY(button && button->defaultAction());
		QVERIFY(button->defaultAction()->text().startsWith("Pitch Bend mode"));
		QVERIFY(QMetaObject::invokeMethod(button->menu(), "aboutToShow", Qt::DirectConnection));
		QAction* straight = nullptr;
		for (auto* action : button->menu()->actions())
		{
			if (action->text() == "Straight lines") { straight = action; }
			if (action->text() == "Smooth curves") { QVERIFY(action->isChecked()); }
		}
		QVERIFY(straight && !straight->isChecked());
		straight->trigger();
		QCOMPARE(clip.notes().front()->detuning()->automationClip()->progressionType(),
			AutomationClip::ProgressionType::Linear);
		window->setCurrentMidiClip(nullptr);
	}

	//! Saves screenshots of the pitch bend editor when PIANOROLL_TEST_SNAPSHOTS=<dir> is set
	void pitchBendSnapshots()
	{
		const QString dir = qEnvironmentVariable("PIANOROLL_TEST_SNAPSHOTS");
		if (dir.isEmpty()) { QSKIP("Set PIANOROLL_TEST_SNAPSHOTS to save screenshots"); }
		auto guard = Engine::audioEngine()->requestChangesGuard();
		struct TestContainer : TrackContainer
		{
			QString nodeName() const override { return "trackcontainer"; }
		} container;
		InstrumentTrack track{&container};
		MidiClip clip{&track};
		clip.clearNotes();
		auto* rise = clip.addNote(Note{TimePos{192}, TimePos{0}, 60}, false);
		rise->createDetuning();
		auto* curve = rise->detuning()->automationClip();
		curve->putValue(0, 0, false);
		curve->putValue(96, 7, false);
		curve->putValue(191, 7, false);
		auto* steep = clip.addNote(Note{TimePos{192}, TimePos{192}, 64}, false);
		steep->createDetuning();
		steep->detuning()->automationClip()->putValue(0, 0, false);
		steep->detuning()->automationClip()->putValue(24, -5, false);
		steep->detuning()->automationClip()->putValue(150, -5, false);
		steep->detuning()->automationClip()->setNodeTangent(24, false, -2.f, false);
		clip.addNote(Note{TimePos{96}, TimePos{384}, 55}, false);
		clip.addNote(Note{TimePos{96}, TimePos{384}, 59}, false);
		rise->setSelected(true);
		steep->setSelected(true);

		auto* window = m_gui->pianoRoll();
		window->setCurrentMidiClip(&clip);
		window->resize(1100, 640);
		window->show();
		auto* roll = window->findChild<gui::PianoRoll*>();
		QVERIFY(roll);
		QVERIFY(QMetaObject::invokeMethod(roll, "setEditMode", Qt::DirectConnection,
			Q_ARG(int, static_cast<int>(gui::PianoRoll::EditMode::Detuning))));
		QTest::qWait(50);
		QVERIFY(window->grab().save(dir + "/pitchbend.png"));

		// Hover a curve handle
		const auto handles = hits(roll, Qt::CrossCursor);
		QVERIFY(!handles.empty());
		send(roll, QEvent::MouseMove, handles.front(), Qt::NoButton, Qt::NoButton);
		QVERIFY(roll->grab().save(dir + "/pitchbend_hover.png"));

		auto* button = window->findChild<QToolButton*>("pianoRollGlideMenuButton");
		QVERIFY(QMetaObject::invokeMethod(button->menu(), "aboutToShow", Qt::DirectConnection));
		button->menu()->adjustSize();
		QVERIFY(button->menu()->grab().save(dir + "/glide_menu.png"));

		QTimer::singleShot(0, [&]
		{
			auto* dialog = QApplication::activeModalWidget();
			QTest::qWait(20);
			dialog->grab().save(dir + "/humanize.png");
			static_cast<QDialog*>(dialog)->reject();
		});
		QMetaObject::invokeMethod(roll, "humanizeNotes", Qt::DirectConnection);
		window->setCurrentMidiClip(nullptr);
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
