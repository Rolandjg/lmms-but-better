/* Copyright (c) 2026 LMMS developers. SPDX-License-Identifier: GPL-2.0-or-later */
#include <QtTest>
#include <QDomDocument>
#include <QPushButton>
#include <cmath>
#include <memory>

#include "AudioDummy.h"
#include "ConfigManager.h"
#include "Effect.h"
#include "EffectControlDialog.h"
#include "EffectControls.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "Knob.h"
#include "LeftRightNav.h"
#include "MainApplication.h"
#include "MainWindow.h"
#include "NotePlayHandle.h"

using namespace lmms;

//! Drives the Formant Shifter's editor with the mouse. The plugin's classes are private to the
//! plugin, so it is reached through widget class names and its saved settings.
class FormantShifterViewTest : public QObject
{
	Q_OBJECT
	QTemporaryDir m_configDir;
	std::unique_ptr<gui::GuiApplication> m_gui;
	std::unique_ptr<Model> m_parent;
	Effect* m_effect = nullptr;
	gui::EffectControlDialog* m_dialog = nullptr;
	QWidget* m_display = nullptr;
	QWidget* m_vowels = nullptr;

	QDomElement state()
	{
		static QDomDocument doc;
		auto parent = doc.createElement("parent");
		return m_effect->controls()->saveState(doc, parent);
	}

	double param(const QString& name)
	{
		// Models with a log scale (or automation) are saved as elements instead of attributes
		const auto element = state();
		const auto child = element.firstChildElement(name);
		return (child.isNull() ? element.attribute(name) : child.attribute("value")).toDouble();
	}

	void setParam(const QString& name, const QString& value)
	{
		auto element = state();
		element.setAttribute(name, value);
		m_effect->controls()->restoreState(element);
		QApplication::processEvents();
	}

	void send(QWidget* w, QEvent::Type type, QPoint pos, Qt::MouseButtons buttons, Qt::MouseButton button = Qt::LeftButton)
	{
		QMouseEvent event(type, pos, w->mapToGlobal(pos), button, buttons, Qt::NoModifier);
		QApplication::sendEvent(w, &event);
	}

	void drag(QWidget* w, QPoint from, QPoint to)
	{
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

	void wheel(QWidget* w, int delta)
	{
		const QPoint pos(w->width() / 2, w->height() / 2);
		QWheelEvent event(pos, w->mapToGlobal(pos), QPoint(), QPoint(0, delta), Qt::NoButton, Qt::NoModifier,
			Qt::NoScrollPhase, false);
		QApplication::sendEvent(w, &event);
		QApplication::processEvents();
	}

	gui::Knob* knob(const QString& displayName)
	{
		for (auto k : m_dialog->findChildren<gui::Knob*>())
		{
			if (k->model() && k->model()->displayName() == displayName) { return k; }
		}
		return nullptr;
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

		m_parent = std::make_unique<Model>(nullptr);
		m_effect = Effect::instantiate("formantshifter", m_parent.get(), nullptr);
		QVERIFY(m_effect);
		QCOMPARE(m_effect->descriptor()->name, "formantshifter");
		m_dialog = m_effect->controls()->createView();
		m_dialog->show();
		QApplication::processEvents();
		for (auto widget : m_dialog->findChildren<QWidget*>())
		{
			const QString name = widget->metaObject()->className();
			if (name == "lmms::gui::FormantDisplay") { m_display = widget; }
			if (name == "lmms::gui::VowelStrip") { m_vowels = widget; }
		}
		QVERIFY(m_display);
		QVERIFY(m_vowels);
	}

	void spectralModeReportsOneFrameOfLatency()
	{
		QVERIFY(m_effect->latency() >= 2048);
		setParam("mode", "1");
		QCOMPARE(m_effect->latency(), f_cnt_t{0});
		setParam("mode", "0");
	}

	void draggingTheDisplayShiftsTheFormants()
	{
		const int y = m_display->height() / 2;
		const int dx = m_display->width() / 10;
		drag(m_display, {100, y}, {100 + dx, y});
		// A tenth of the width is a tenth of the ~10 octaves on the axis
		const double expected = 12.0 * std::log2(1000.0) * dx / (m_display->width() - 2);
		QVERIFY2(std::abs(param("formant") - expected) < 0.2, qPrintable(QString::number(param("formant"))));

		drag(m_display, {300, y}, {300 - 2 * dx, y});
		QVERIFY(std::abs(param("formant") + expected) < 0.3);

		doubleClick(m_display, {200, y});
		QCOMPARE(param("formant"), 0.0);
	}

	void scrollingTheDisplayMorphsTheVowel()
	{
		setParam("vowel", "1");
		wheel(m_display, 120);
		QVERIFY(std::abs(param("vowel") - 1.1) < 1e-3);
		wheel(m_display, -120);
		wheel(m_display, -120);
		QVERIFY(std::abs(param("vowel") - 0.9) < 1e-3);
	}

	void vowelStripSetsAndSnapsTheVowel()
	{
		const int cell = m_vowels->width() / 5;
		const int y = m_vowels->height() / 2;
		// Click the middle of the "O"
		send(m_vowels, QEvent::MouseButtonPress, {cell * 3 + cell / 2, y}, Qt::LeftButton);
		send(m_vowels, QEvent::MouseButtonRelease, {cell * 3 + cell / 2, y}, Qt::NoButton);
		QVERIFY(std::abs(param("vowel") - 3.0) < 0.02);

		drag(m_vowels, {cell / 2, y}, {cell + cell / 4, y});
		QVERIFY(std::abs(param("vowel") - 0.75) < 0.03);

		doubleClick(m_vowels, {cell + cell / 3, y});
		QCOMPARE(param("vowel"), 1.0);
	}

	void presetArrowsApplyWholePresets()
	{
		setParam("preset", "0");
		auto nav = m_dialog->findChild<gui::LeftRightNav*>();
		QVERIFY(nav);
		const auto buttons = nav->findChildren<QPushButton*>();
		QCOMPARE(buttons.size(), 2);
		buttons[1]->click(); // "Helium"
		QApplication::processEvents();
		QCOMPARE(param("preset"), 1.0);
		QCOMPARE(param("formant"), 6.0);
		buttons[0]->click();
		buttons[0]->click(); // wraps around to the last preset, a resonant one
		QApplication::processEvents();
		QVERIFY(param("preset") > 20.0);
		QCOMPARE(param("mode"), 1.0);
		QVERIFY(param("res") > 1.0);
	}

	void loadingAProjectKeepsItsValuesOverThePreset()
	{
		auto element = state();
		element.setAttribute("preset", "1");   // Helium sets formant to +6...
		element.setAttribute("formant", "-3"); // ...but the project saved -3
		m_effect->controls()->restoreState(element);
		QCOMPARE(param("formant"), -3.0);
	}

	void spectralOnlyControlsGreyOutInResonantMode()
	{
		setParam("mode", "1");
		QVERIFY(knob("Pitch"));
		QVERIFY(!knob("Pitch")->isEnabled());
		QVERIFY(!knob("Imprint")->isEnabled());
		QVERIFY(knob("Formant")->isEnabled());
		setParam("mode", "0");
		QVERIFY(knob("Pitch")->isEnabled());

		setParam("lfo_sync", "5");
		QVERIFY(!knob("LFO rate")->isEnabled());
		setParam("lfo_sync", "0");
		QVERIFY(knob("LFO rate")->isEnabled());
	}

	void cleanupTestCase()
	{
		delete m_dialog;
		delete m_effect;
		m_gui->mainWindow()->deleteLater();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		m_gui.reset();
		NotePlayHandleManager::free();
	}
};

int main(int argc, char** argv)
{
	gui::MainApplication app{argc, argv};
	FormantShifterViewTest test;
	return QTest::qExec(&test, argc, argv);
}
#include "FormantShifterViewTest.moc"
