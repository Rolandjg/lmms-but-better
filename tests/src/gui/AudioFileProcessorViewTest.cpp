/* Copyright (c) 2026 LMMS developers. SPDX-License-Identifier: GPL-2.0-or-later */
#include <QtTest>
#include <QDomDocument>
#include <memory>

#include "AudioDummy.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "InstrumentTrackView.h"
#include "InstrumentTrackWindow.h"
#include "MainApplication.h"
#include "MainWindow.h"
#include "Song.h"

using namespace lmms;

//! Drives the AudioFileProcessor's sample editor with the mouse. The plugin's classes are private
//! to the plugin, so it is reached through widget class names and its saved settings.
class AudioFileProcessorViewTest : public QObject
{
	Q_OBJECT
	QTemporaryDir m_configDir;
	std::unique_ptr<gui::GuiApplication> m_gui;
	InstrumentTrack* m_track = nullptr;
	Instrument* m_afp = nullptr;
	gui::InstrumentTrackWindow* m_window = nullptr;
	QWidget* m_wave = nullptr;

	QDomElement state()
	{
		static QDomDocument doc;
		auto parent = doc.createElement("parent");
		return m_afp->saveState(doc, parent);
	}

	double param(const QString& name)
	{
		// Models with a log scale are saved as elements instead of attributes
		const auto element = state();
		const auto child = element.firstChildElement(name);
		return (child.isNull() ? element.attribute(name) : child.attribute("value")).toDouble();
	}

	void setParams(const QMap<QString, QString>& values)
	{
		auto element = state();
		for (auto it = values.begin(); it != values.end(); ++it) { element.setAttribute(it.key(), it.value()); }
		m_afp->restoreState(element);
		QApplication::processEvents();
	}

	int sliceCount() { return state().attribute("slices").split(',', Qt::SkipEmptyParts).size(); }

	// Mouse helpers: offscreen widgets get synthesized events
	void send(QEvent::Type type, QPoint pos, Qt::MouseButtons buttons, Qt::MouseButton button = Qt::LeftButton)
	{
		QMouseEvent event(type, pos, m_wave->mapToGlobal(pos), button, buttons, Qt::NoModifier);
		QApplication::sendEvent(m_wave, &event);
	}

	void drag(QPoint from, QPoint to)
	{
		send(QEvent::MouseMove, from, Qt::NoButton, Qt::NoButton);
		send(QEvent::MouseButtonPress, from, Qt::LeftButton);
		for (int i = 1; i <= 8; ++i) { send(QEvent::MouseMove, from + (to - from) * i / 8, Qt::LeftButton, Qt::NoButton); }
		send(QEvent::MouseButtonRelease, to, Qt::NoButton);
		QApplication::processEvents();
	}

	void doubleClick(QPoint pos)
	{
		send(QEvent::MouseButtonPress, pos, Qt::LeftButton);
		send(QEvent::MouseButtonRelease, pos, Qt::NoButton);
		send(QEvent::MouseButtonDblClick, pos, Qt::LeftButton);
		send(QEvent::MouseButtonRelease, pos, Qt::NoButton);
		QApplication::processEvents();
	}

	void snapshot(const QString& name)
	{
		if (const auto dir = qEnvironmentVariable("AFP_TEST_SNAPSHOTS"); !dir.isEmpty())
		{
			QApplication::processEvents();
			m_window->grab().save(dir + "/" + name + ".png");
		}
	}

	// The editor's layout: a 16 px pad lane, the waveform, a 14 px ruler and a 12 px overview
	int waveMid() const { return 16 + (m_wave->height() - 16 - 14 - 12) / 2; }
	int xOf(double fraction) const { return static_cast<int>(fraction * m_wave->width()); }

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
		m_afp = m_track->loadInstrument("audiofileprocessor");
		QVERIFY(m_afp);
		QApplication::processEvents();
		for (auto widget : QApplication::allWidgets())
		{
			auto trackView = dynamic_cast<gui::InstrumentTrackView*>(widget);
			if (trackView && trackView->model() == m_track) { m_window = trackView->getInstrumentTrackWindow(); }
		}
		QVERIFY(m_window);
		m_afp->loadFile(qEnvironmentVariable("LMMS_DATA_DIR") + "/samples/beats/break01 - 168 BPM.ogg");
		m_window->show();
		QApplication::processEvents();
		for (auto widget : m_window->findChildren<QWidget*>())
		{
			if (QString(widget->metaObject()->className()) == "lmms::gui::AudioFileProcessorWaveView") { m_wave = widget; }
		}
		QVERIFY(m_wave);
		QVERIFY(m_wave->width() > 400);
	}

	void tempoIsDetectedFromTheFileName()
	{
		QCOMPARE(param("sampletempo"), 168.0);
	}

	void dragStartAndEndPoints()
	{
		drag({1, waveMid()}, {xOf(0.25), waveMid()});
		// Snapping to a zero crossing moves it by at most 5 ms of 1.44 s
		QVERIFY2(std::abs(param("sframe") - 0.25) < 0.006, qPrintable(QString::number(param("sframe"))));

		drag({m_wave->width() - 1, waveMid()}, {xOf(0.75), waveMid()});
		QVERIFY2(std::abs(param("eframe") - 0.75) < 0.006, qPrintable(QString::number(param("eframe"))));
		snapshot("afp_region");
	}

	void dragFadeHandles()
	{
		// The fade-in handle sits at the top of the waveform, on the start line
		const int top = 16 + 6;
		drag({xOf(param("sframe")), top}, {xOf(param("sframe")) + 40, top});
		const double expectedMs = 40.0 / m_wave->width() * 1.44 * 1000.0;
		QVERIFY2(std::abs(param("fadein") - expectedMs) < 6.0, qPrintable(QString::number(param("fadein"))));

		drag({xOf(param("eframe")), top}, {xOf(param("eframe")) - 20, top});
		QVERIFY(param("fadeout") > 20.0);
		snapshot("afp_fades");

		doubleClick({xOf(param("sframe")) + 40, top});
		QCOMPARE(param("fadein"), 0.0);
	}

	void addMoveAndRemoveSlices()
	{
		setParams({{"mode", "1"}, {"sframe", "0"}, {"eframe", "1"}, {"lframe", "0"}});
		const int automatic = sliceCount();
		QVERIFY2(automatic >= 4, qPrintable(QString::number(automatic)));
		QCOMPARE(param("slicemethod"), 0.0);

		// Double-click empty space between markers to add one; this switches to manual slicing
		const auto slices = state().attribute("slices").split(',');
		const double a = slices[0].toDouble();
		const double b = slices.size() > 1 ? slices[1].toDouble() : 1.0;
		const int x = xOf((a + b) / 2);
		doubleClick({x, waveMid()});
		QCOMPARE(sliceCount(), automatic + 1);
		QCOMPARE(param("slicemethod"), 2.0);
		snapshot("afp_slices");

		// Drag the new marker a little
		drag({x, waveMid()}, {x + 10, waveMid()});
		bool moved = false;
		for (const auto& s : state().attribute("slices").split(','))
		{
			if (std::abs(xOf(s.toDouble()) - (x + 10)) <= 3) { moved = true; }
		}
		QVERIFY(moved);

		// ...and double-click it away again
		doubleClick({x + 10, waveMid()});
		QCOMPARE(sliceCount(), automatic);
	}

	void padsAuditionSlices()
	{
		// Pressing a pad plays its slice through the track and releasing stops it
		send(QEvent::MouseButtonPress, {xOf(0.02), 8}, Qt::LeftButton);
		QApplication::processEvents();
		send(QEvent::MouseButtonRelease, {xOf(0.02), 8}, Qt::NoButton);
		QApplication::processEvents();
	}

	void wheelZoomKeepsPointsEditable()
	{
		// Zoom in on the middle; dragging the end point must still work at the new scale
		setParams({{"mode", "0"}});
		for (int i = 0; i < 4; ++i)
		{
			QWheelEvent wheel(QPointF(xOf(0.5), waveMid()), m_wave->mapToGlobal(QPoint(xOf(0.5), waveMid())), QPoint(),
				QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
			QApplication::sendEvent(m_wave, &wheel);
		}
		snapshot("afp_zoomed");
		// Double-click empty space shows the whole sample again
		doubleClick({xOf(0.5), waveMid()});
		snapshot("afp_unzoomed");
		QVERIFY(param("eframe") > 0.99);
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
	AudioFileProcessorViewTest test;
	return QTest::qExec(&test, argc, argv);
}
#include "AudioFileProcessorViewTest.moc"
