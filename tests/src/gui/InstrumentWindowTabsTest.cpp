/* Copyright (c) 2026 LMMS developers. SPDX-License-Identifier: GPL-2.0-or-later */
#include <QtTest>
#include <QDomDocument>
#include <memory>

#include "AudioDummy.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "EnvelopeAndLfoParameters.h"
#include "EnvelopeGraph.h"
#include "GuiApplication.h"
#include "InstrumentFunctions.h"
#include "InstrumentTrack.h"
#include "InstrumentTrackView.h"
#include "InstrumentTrackWindow.h"
#include "LfoGraph.h"
#include "MainApplication.h"
#include "MainWindow.h"
#include "ModernWidgets.h"
#include "Song.h"
#include "TabWidget.h"

using namespace lmms;

//! Drives the envelope and arpeggio tabs of the instrument window with the mouse and checks the
//! track settings they change
class InstrumentWindowTabsTest : public QObject
{
	Q_OBJECT
	QTemporaryDir m_configDir;
	std::unique_ptr<gui::GuiApplication> m_gui;
	InstrumentTrack* m_track = nullptr;
	gui::InstrumentTrackWindow* m_window = nullptr;

	//! An attribute of the first element called @p tag in the saved track
	double setting(const QString& tag, const QString& attribute)
	{
		static QDomDocument doc;
		auto parent = doc.createElement("parent");
		const auto element = m_track->saveState(doc, parent);
		return element.elementsByTagName(tag).item(0).toElement().attribute(attribute).toDouble();
	}

	template<class T>
	T* visible()
	{
		// The main window is never shown offscreen, so lay the instrument window out by rendering it
		m_window->grab();
		for (auto widget : m_window->findChildren<T*>())
		{
			if (widget->isVisibleTo(m_window)) { return widget; }
		}
		return nullptr;
	}

	void send(QWidget* w, QEvent::Type type, QPoint pos, Qt::MouseButtons buttons, Qt::MouseButton button = Qt::LeftButton)
	{
		QMouseEvent event(type, pos, w->mapToGlobal(pos), button, buttons, Qt::NoModifier);
		QApplication::sendEvent(w, &event);
	}

	void drag(QWidget* w, QPoint from, QPoint to)
	{
		send(w, QEvent::MouseMove, from, Qt::NoButton, Qt::NoButton);
		send(w, QEvent::MouseButtonPress, from, Qt::LeftButton);
		for (int i = 1; i <= 8; ++i) { send(w, QEvent::MouseMove, from + (to - from) * i / 8, Qt::LeftButton, Qt::NoButton); }
		send(w, QEvent::MouseButtonRelease, to, Qt::NoButton);
		QApplication::processEvents();
	}

	void doubleClick(QWidget* w, QPoint pos)
	{
		send(w, QEvent::MouseButtonPress, pos, Qt::LeftButton);
		send(w, QEvent::MouseButtonRelease, pos, Qt::NoButton);
		send(w, QEvent::MouseButtonDblClick, pos, Qt::LeftButton);
		send(w, QEvent::MouseButtonRelease, pos, Qt::NoButton);
		QApplication::processEvents();
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

		m_track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(m_track);
		QVERIFY(m_track->loadInstrument("tripleoscillator"));
		QApplication::processEvents();
		for (auto widget : QApplication::allWidgets())
		{
			auto trackView = dynamic_cast<gui::InstrumentTrackView*>(widget);
			if (trackView && trackView->model() == m_track) { m_window = trackView->getInstrumentTrackWindow(); }
		}
		QVERIFY(m_window);
		m_window->show();
		QApplication::processEvents();
	}

	void dragEnvelopePoints()
	{
		m_window->tabWidgetParent()->setActiveTab(1);
		QApplication::processEvents();
		auto graph = visible<gui::EnvelopeGraph>();
		QVERIFY(graph);
		QVERIFY2(graph->height() > 40 && graph->width() > 200, "the window was not laid out");
		QCOMPARE(setting("elvol", "amt"), 0.0);

		// Default envelope: no pre-delay or attack, so the attack point is the top left corner
		// of the plot area (inset 6 px left, 8 px top). Dragging it switches the envelope on.
		const QPoint attack(6, 8);
		drag(graph, attack, attack + QPoint(30, 0));
		QVERIFY2(setting("elvol", "att") > 0.1, qPrintable(QString::number(setting("elvol", "att"))));
		QCOMPARE(setting("elvol", "amt"), 1.0);

		// Double-clicking a point resets it
		doubleClick(graph, attack + QPoint(30, 0));
		QCOMPARE(setting("elvol", "att"), 0.0);

		// Double-clicking the background switches the envelope off again
		doubleClick(graph, QPoint(graph->width() - 10, graph->height() / 2));
		QCOMPARE(setting("elvol", "amt"), 0.0);
	}

	void bendEnvelopeCurves()
	{
		auto graph = visible<gui::EnvelopeGraph>();
		QVERIFY(graph);
		QCOMPARE(setting("elvol", "deccurve"), 0.0);

		// Default envelope: hold and decay are a quarter of their range each, release 5% and sustain
		// 50%. The envelope is fitted to the plot width (inset 6/8/6/6 px), and the decay curve
		// handle sits halfway along the decay (drawn shortened by the sustain), at 75% height.
		const double plotWidth = graph->width() - 12, plotHeight = graph->height() - 14;
		const double segment = plotWidth / (0.25 + 0.25 + 0.05);
		const QPoint handle(qRound(6 + 0.25 * segment + 0.0625 * segment), qRound(8 + 0.25 * plotHeight));
		drag(graph, handle, handle - QPoint(0, 20));
		QVERIFY2(std::abs(setting("elvol", "deccurve") - 0.5) < 0.03, qPrintable(QString::number(setting("elvol", "deccurve"))));
		QCOMPARE(setting("elvol", "amt"), 1.0);

		// The handle rides on the bent curve
		const float level = EnvelopeAndLfoParameters::curveValue(0.5f, 1.f, 0.5f, setting("elvol", "deccurve"));
		doubleClick(graph, QPoint(handle.x(), qRound(8 + (1 - level) * plotHeight)));
		QCOMPARE(setting("elvol", "deccurve"), 0.0);
	}

	void curveShapes()
	{
		using Env = EnvelopeAndLfoParameters;
		QCOMPARE(Env::curveValue(0.25f, 0.f, 1.f, 0.f), 0.25f);
		// Bending up makes a rising segment start fast and a falling one start slow
		QVERIFY(Env::curveValue(0.5f, 0.f, 1.f, 1.f) > 0.95f);
		QVERIFY(Env::curveValue(0.5f, 1.f, 0.f, 1.f) > 0.95f);
		QVERIFY(Env::curveValue(0.5f, 1.f, 0.f, -1.f) < 0.05f);
		QCOMPARE(Env::curveValue(1.f, 0.2f, 0.8f, 0.7f), 0.8f);
	}

	void curvesShapeTheRenderedEnvelope()
	{
		// Level halfway through the attack, straight and bent upwards
		EnvelopeAndLfoParameters params(1.f, nullptr);
		params.getAmountModel().setValue(1.f);
		params.getAttackModel().setValue(0.3f);
		params.getHoldModel().setValue(0.f);
		const auto attackFrames = static_cast<f_cnt_t>(5.f * 0.09f * Engine::audioEngine()->outputSampleRate());
		auto levelAt = [&](f_cnt_t frame)
		{
			float level = 0.f;
			params.fillLevel(&level, frame, frame + 1000000, 1);
			return level;
		};
		const float straight = levelAt(attackFrames / 2);
		QVERIFY2(std::abs(straight - 0.5f) < 0.01f, qPrintable(QString::number(straight)));
		params.getAttackCurveModel().setValue(0.8f);
		const float bent = levelAt(attackFrames / 2);
		QVERIFY2(std::abs(bent - EnvelopeAndLfoParameters::curveValue(0.5f, 0.f, 1.f, 0.8f)) < 0.01f,
			qPrintable(QString::number(bent)));
		// Release, bent downwards: it drops fast at first
		params.getReleaseCurveModel().setValue(-0.8f);
		params.getSustainModel().setValue(1.f);
		const auto releaseStart = attackFrames * 2;
		const auto releaseFrames = static_cast<f_cnt_t>(5.f * params.getReleaseModel().value() * params.getReleaseModel().value()
			* Engine::audioEngine()->outputSampleRate());
		float level = 0.f;
		params.fillLevel(&level, releaseStart + releaseFrames / 2, releaseStart, 1);
		QVERIFY2(level < 0.2f, qPrintable(QString::number(level)));
	}

	void dragLfoAmount()
	{
		auto graph = visible<gui::LfoGraph>();
		QVERIFY(graph);
		const QPoint mid(graph->width() / 2, graph->height() / 2);
		drag(graph, mid, mid - QPoint(0, 25));
		QVERIFY2(std::abs(setting("elvol", "lamt") - 0.5) < 0.02, qPrintable(QString::number(setting("elvol", "lamt"))));
	}

	void arpeggioHeaderSwitchesItOn()
	{
		m_window->tabWidgetParent()->setActiveTab(2);
		QApplication::processEvents();
		gui::ModernHeader* header = nullptr;
		for (auto widget : m_window->findChildren<gui::ModernHeader*>())
		{
			if (widget->isVisibleTo(m_window) && widget->text() == "ARPEGGIO") { header = widget; }
		}
		QVERIFY(header);
		QCOMPARE(setting("arpeggiator", "arp-enabled"), 0.0);
		send(header, QEvent::MouseButtonPress, {4, 8}, Qt::LeftButton);
		send(header, QEvent::MouseButtonRelease, {4, 8}, Qt::NoButton);
		QApplication::processEvents();
		QCOMPARE(setting("arpeggiator", "arp-enabled"), 1.0);
	}

	void arpeggioSteps()
	{
		using Dir = InstrumentFunctionArpeggio::ArpDirection;
		auto steps = [](Dir dir, int range, float repeats = 1, float cycle = 0)
		{
			QList<int> result;
			for (int step = 0; step < 6; ++step)
			{
				result << InstrumentFunctionArpeggio::noteIndexForStep(step, dir, range, repeats, cycle);
			}
			return result;
		};
		QCOMPARE(steps(Dir::Up, 3), (QList<int>{0, 1, 2, 0, 1, 2}));
		QCOMPARE(steps(Dir::Down, 3), (QList<int>{2, 1, 0, 2, 1, 0}));
		QCOMPARE(steps(Dir::UpAndDown, 3), (QList<int>{0, 1, 2, 1, 0, 1}));
		QCOMPARE(steps(Dir::Up, 4, 2), (QList<int>{0, 0, 1, 1, 0, 0}));
		QCOMPARE(steps(Dir::Up, 3, 1, 1), (QList<int>{0, 2, 1, 0, 2, 1}));
		// A single note repeated twice used to divide by zero in up-and-down mode
		QCOMPARE(steps(Dir::UpAndDown, 2, 2), (QList<int>{0, 0, 0, 0, 0, 0}));
	}

	void cleanupTestCase()
	{
		m_window->hide();
		m_gui->mainWindow()->deleteLater();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		m_gui.reset();
		NotePlayHandleManager::free();
	}
};

int main(int argc, char** argv)
{
	gui::MainApplication app{argc, argv};
	InstrumentWindowTabsTest test;
	return QTest::qExec(&test, argc, argv);
}
#include "InstrumentWindowTabsTest.moc"
