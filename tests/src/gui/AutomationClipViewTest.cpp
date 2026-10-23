/* Copyright (c) 2026 LMMS developers. SPDX-License-Identifier: GPL-2.0-or-later */
#include <QtTest>
#include <QMouseEvent>
#include <memory>

#include "AudioDummy.h"
#include "AutomationClip.h"
#include "AutomationClipView.h"
#include "AutomationTrack.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "MainApplication.h"
#include "MainWindow.h"
#include "ProjectJournal.h"
#include "Song.h"
#include "SongEditor.h"

using namespace lmms;

//! Editing automation points right on a clip in the Song Editor
class AutomationClipViewTest : public QObject
{
	Q_OBJECT
	QTemporaryDir m_configDir;
	std::unique_ptr<gui::GuiApplication> m_gui;
	AutomationTrack* m_track = nullptr;
	AutomationClip* m_clip = nullptr;
	gui::AutomationClipView* m_view = nullptr;

	static constexpr int TrackHeight = 80;
	static constexpr int Border = gui::ClipView::BORDER_WIDTH;

	gui::SongEditor* songEditor() { return m_gui->songEditor()->m_editor; }

	QPoint pointAt(int tick, float value) const
	{
		const float ppt = m_gui->songEditor()->m_editor->pixelsPerBar() / TimePos::ticksPerBar();
		const float y = Border + (1.f - value) * (m_view->height() - 2 * Border);
		return {static_cast<int>(std::lround(tick * ppt)), static_cast<int>(std::lround(y))};
	}

	void send(QEvent::Type type, QPoint pos, Qt::MouseButton button, Qt::MouseButtons buttons,
		Qt::KeyboardModifiers modifiers = Qt::NoModifier)
	{
		QMouseEvent event(type, pos, m_view->mapToGlobal(pos), button, buttons, modifiers);
		QApplication::sendEvent(m_view, &event);
	}

	void hover(QPoint pos) { send(QEvent::MouseMove, pos, Qt::NoButton, Qt::NoButton); }

	void click(QPoint pos, Qt::MouseButton button = Qt::LeftButton, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
	{
		hover(pos);
		send(QEvent::MouseButtonPress, pos, button, button, modifiers);
		send(QEvent::MouseButtonRelease, pos, button, Qt::NoButton, modifiers);
	}

	//! Like a real mouse: the screen points are fixed, while the clip may move underneath them
	void drag(QPoint from, QPoint to)
	{
		const QPoint globalFrom = m_view->mapToGlobal(from);
		const QPoint globalTo = m_view->mapToGlobal(to);
		const auto local = [this](QPoint global) { return m_view->mapFromGlobal(global); };
		hover(from);
		send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
		send(QEvent::MouseMove, local((globalFrom + globalTo) / 2), Qt::NoButton, Qt::LeftButton);
		send(QEvent::MouseMove, local(globalTo), Qt::NoButton, Qt::LeftButton);
		send(QEvent::MouseButtonRelease, local(globalTo), Qt::LeftButton, Qt::NoButton);
	}

	bool hasNode(int tick, float value) const
	{
		const auto& timeMap = m_clip->getTimeMap();
		const auto it = timeMap.constFind(tick);
		return it != timeMap.constEnd() && std::abs(it->getInValue() - value) < 0.02f;
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

	// A fresh 4 bar clip with a linear ramp from 0 at the start to 1 at bar 2
	void init()
	{
		AutomationClip::setQuantization(48);
		m_track = dynamic_cast<AutomationTrack*>(Track::create(Track::Type::Automation, Engine::getSong()));
		QVERIFY(m_track);
		m_track->setHeight(TrackHeight);
		m_clip = new AutomationClip(m_track);
		m_clip->changeLength(TimePos(4 * TimePos::ticksPerBar()));
		m_clip->setAutoResize(false);
		m_clip->setProgressionType(AutomationClip::ProgressionType::Linear);
		m_clip->putValue(TimePos(0), 0.f);
		m_clip->putValue(TimePos(2 * TimePos::ticksPerBar()), 1.f);
		for (int i = 0; i < 5; ++i) { QCoreApplication::processEvents(); }

		m_view = nullptr;
		for (auto view : m_gui->mainWindow()->findChildren<gui::AutomationClipView*>())
		{
			if (view->getClip() == m_clip) { m_view = view; }
		}
		QVERIFY(m_view);
		QVERIFY(m_view->height() >= TrackHeight - 2 * Border);
		songEditor()->setEditModeDraw();
	}

	void cleanup()
	{
		delete m_track;
		m_track = nullptr;
		m_clip = nullptr;
		m_view = nullptr;
		for (int i = 0; i < 5; ++i) { QCoreApplication::processEvents(); }
	}

	void hoverShowsWhatAClickDoes()
	{
		hover(pointAt(2 * TimePos::ticksPerBar(), 1.f));
		QCOMPARE(m_view->cursor().shape(), Qt::OpenHandCursor);
		hover(pointAt(TimePos::ticksPerBar(), 0.5f));
		QCOMPARE(m_view->cursor().shape(), Qt::CrossCursor);
		// far above the ramp: the clip itself is grabbed
		hover(pointAt(TimePos::ticksPerBar() / 2, 0.95f));
		QCOMPARE(m_view->cursor().shape(), Qt::PointingHandCursor);
	}

	void hoverDrawsThePoints()
	{
		const QPoint outside = pointAt(3 * TimePos::ticksPerBar(), 0.2f);
		QEvent leave(QEvent::Leave);
		QApplication::sendEvent(m_view, &leave);
		const QImage plain = m_view->grab().toImage();
		QEvent enter(QEvent::Enter);
		QApplication::sendEvent(m_view, &enter);
		hover(outside);
		const QImage hovered = m_view->grab().toImage();
		QVERIFY(plain != hovered);
	}

	void draggingAPointMovesIt()
	{
		const TimePos start = m_clip->startPosition();
		// The time follows the mouse while the value moves at a third of its speed
		drag(pointAt(2 * TimePos::ticksPerBar(), 1.f), pointAt(3 * TimePos::ticksPerBar(), 0.5f));
		QVERIFY(hasNode(3 * TimePos::ticksPerBar(), 1.f - 0.5f / 3));
		QVERIFY(!m_clip->getTimeMap().contains(2 * TimePos::ticksPerBar()));
		QCOMPARE(m_clip->getTimeMap().size(), 2);
		QVERIFY(!m_clip->isDragging());
		QCOMPARE(m_clip->startPosition().getTicks(), start.getTicks());
	}

	void draggingThreeClipHeightsSweepsTheRange()
	{
		const int tick = 2 * TimePos::ticksPerBar();
		const int span = m_view->height() - 2 * Border;
		drag(pointAt(tick, 1.f), pointAt(tick, 1.f) + QPoint(0, span));
		QVERIFY(hasNode(tick, 2.f / 3));
		drag(pointAt(tick, 2.f / 3), pointAt(tick, 2.f / 3) + QPoint(0, 2 * span));
		QVERIFY(hasNode(tick, 0.f));
		// dragging further than the range just stays at the end of it
		drag(pointAt(tick, 0.f), pointAt(tick, 0.f) - QPoint(0, 5 * span));
		QVERIFY(hasNode(tick, 1.f));
	}

	void newClipsAreSmoothCurves()
	{
		auto clip = dynamic_cast<AutomationClip*>(m_track->createClip(TimePos(8 * TimePos::ticksPerBar())));
		// let the queued view creation run while the track still exists
		for (int i = 0; i < 5; ++i) { QCoreApplication::processEvents(); }
		QVERIFY(clip);
		QCOMPARE(clip->progressionType(), AutomationClip::ProgressionType::CubicHermite);
	}

	void grabbingAPointOffCenterDoesNotMoveIt()
	{
		// press a few pixels next to the node and release without moving
		const QPoint node = pointAt(2 * TimePos::ticksPerBar(), 1.f);
		click(node + QPoint(3, 2));
		QVERIFY(hasNode(2 * TimePos::ticksPerBar(), 1.f));
		QCOMPARE(m_clip->getTimeMap().size(), 2);
	}

	void clickingTheLineAddsAPoint()
	{
		click(pointAt(TimePos::ticksPerBar(), 0.5f));
		QCOMPARE(m_clip->getTimeMap().size(), 3);
		QVERIFY(hasNode(TimePos::ticksPerBar(), 0.5f));

		Engine::projectJournal()->undo();
		QCOMPARE(m_clip->getTimeMap().size(), 2);
	}

	void clickingAwayFromTheLineLeavesThePoints()
	{
		click(pointAt(TimePos::ticksPerBar() / 2, 0.95f));
		QCOMPARE(m_clip->getTimeMap().size(), 2);
	}

	void shiftClickAddsAPointAnywhere()
	{
		click(pointAt(TimePos::ticksPerBar() / 2, 0.75f), Qt::LeftButton, Qt::ShiftModifier);
		QCOMPARE(m_clip->getTimeMap().size(), 3);
		QVERIFY(hasNode(TimePos::ticksPerBar() / 2, 0.75f));
	}

	void rightClickRemovesAPoint()
	{
		click(pointAt(2 * TimePos::ticksPerBar(), 1.f), Qt::RightButton);
		QCOMPARE(m_clip->getTimeMap().size(), 1);
		QVERIFY(hasNode(0, 0.f));
	}

	void draggingTheClipBodyStillMovesTheClip()
	{
		const TimePos start = m_clip->startPosition();
		const QPoint from = pointAt(TimePos::ticksPerBar() / 2, 0.95f);
		drag(from, from + QPoint(static_cast<int>(2 * songEditor()->pixelsPerBar()), 0));
		QCOMPARE(m_clip->startPosition().getTicks(), start.getTicks() + 2 * TimePos::ticksPerBar());
		QCOMPARE(m_clip->getTimeMap().size(), 2);
	}

	void knifeModeLeavesThePointsAlone()
	{
		songEditor()->setEditModeKnife();
		hover(pointAt(2 * TimePos::ticksPerBar(), 1.f));
		QCOMPARE(m_view->cursor().shape(), Qt::SplitHCursor);
		hover(pointAt(TimePos::ticksPerBar(), 0.5f));
		QCOMPARE(m_view->cursor().shape(), Qt::SplitHCursor);
		songEditor()->setEditModeDraw();
	}

	void cleanupTestCase()
	{
		m_gui->mainWindow()->deleteLater();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		m_gui.reset();
	}
};

int main(int argc, char** argv)
{
	gui::MainApplication app{argc, argv};
	AutomationClipViewTest test;
	return QTest::qExec(&test, argc, argv);
}
#include "AutomationClipViewTest.moc"
