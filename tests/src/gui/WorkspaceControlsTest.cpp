/* Copyright (c) 2026 LMMS developers. SPDX-License-Identifier: GPL-2.0-or-later */
#include <QtTest>
#include <QLabel>
#include <QMdiArea>
#include <QMdiSubWindow>

#include "ControlAccessibility.h"
#include "TilingWindowManager.h"
#include "WheelAccumulator.h"

using namespace lmms::gui;

class WorkspaceControlsTest : public QObject
{
	Q_OBJECT
private slots:
	void focusRingPreservesExistingChildren()
	{
		QWidget control;
		control.resize(200, 100);
		QLabel label("Value", &control);
		label.setGeometry(15, 20, 60, 25);
		const auto original = label.geometry();
		attachFocusRing(&control);
		control.show();
		QFocusEvent focusIn(QEvent::FocusIn, Qt::TabFocusReason);
		QApplication::sendEvent(&control, &focusIn);
		QCOMPARE(label.geometry(), original);
		auto ring = control.findChild<QWidget*>("lmmsFocusRing");
		QVERIFY(ring);
		QVERIFY(ring->isVisible());
		QCOMPARE(ring->geometry(), control.rect());
		control.resize(250, 120);
		QCOMPARE(ring->geometry(), control.rect());
		QFocusEvent focusOut(QEvent::FocusOut);
		QApplication::sendEvent(&control, &focusOut);
		QVERIFY(!ring->isVisible());
		QVERIFY(label.isVisible());
	}

	void smallWheelStepsAccumulateAndReset()
	{
		WheelAccumulator accumulator;
		const auto step = [&](int delta, unsigned long timestamp, Qt::ScrollPhase phase = Qt::NoScrollPhase)
		{
			QWheelEvent event(QPointF{}, QPointF{}, QPoint{}, QPoint(0, delta), Qt::NoButton,
				Qt::NoModifier, phase, false);
			event.setTimestamp(timestamp);
			return accumulator.steps(&event);
		};
		for (int i = 0; i < 7; ++i) { QCOMPARE(step(15, i * 10), 0); }
		QCOMPARE(step(15, 70), 1);
		QCOMPARE(step(60, 80), 0);
		QCOMPARE(step(-60, 90), 0);
		QCOMPARE(step(-60, 100), -1);
		QCOMPARE(step(60, 110), 0);
		QCOMPARE(step(60, 500), 0); // stale gesture
		QCOMPARE(step(120, 510, Qt::ScrollMomentum), 0);
		QCOMPARE(step(60, 520, Qt::ScrollBegin), 0);
		QCOMPARE(step(60, 530), 1);
	}

	void tilingSurvivesHiddenClosedAndReopenedWindows()
	{
		QMdiArea area;
		area.resize(1200, 800);
		area.show();
		TilingWindowManager manager(&area);
		QList<QMdiSubWindow*> windows;
		for (int i = 0; i < 4; ++i)
		{
			auto content = new QWidget;
			content->setMinimumSize(100, 80);
			auto window = area.addSubWindow(content);
			window->show();
			windows << window;
		}
		manager.setEnabled(true);
		const auto check = [&]
		{
			manager.retile();
			for (auto a : windows)
			{
				if (a->isHidden()) { continue; }
				QVERIFY(area.viewport()->rect().contains(a->geometry()));
				for (auto b : windows)
				{
					if (a != b && !b->isHidden()) { QVERIFY(!a->geometry().intersects(b->geometry())); }
				}
			}
		};
		check();
		const auto firstRect = windows[0]->geometry();
		const auto secondRect = windows[1]->geometry();
		const auto send = [&](QEvent::Type type, QPoint pos, Qt::MouseButton button, Qt::MouseButtons buttons)
		{
			QMouseEvent event(type, pos, windows[0]->mapToGlobal(pos), button, buttons, Qt::NoModifier);
			QApplication::sendEvent(windows[0], &event);
		};
		send(QEvent::MouseButtonPress, QPoint(50, 8), Qt::LeftButton, Qt::LeftButton);
		const auto target = windows[0]->mapFrom(area.viewport(), secondRect.center());
		send(QEvent::MouseMove, target, Qt::NoButton, Qt::LeftButton);
		send(QEvent::MouseButtonRelease, target, Qt::LeftButton, Qt::NoButton);
		QCOMPARE(windows[0]->geometry(), secondRect);
		QCOMPARE(windows[1]->geometry(), firstRect);
		check();
		windows[1]->hide();
		check();
		windows[1]->show();
		check();
		delete windows.takeLast();
		check();
		area.resize(900, 600);
		check();
		manager.setEnabled(false);
		manager.setEnabled(true);
		check();
	}
};

QTEST_MAIN(WorkspaceControlsTest)
#include "WorkspaceControlsTest.moc"
