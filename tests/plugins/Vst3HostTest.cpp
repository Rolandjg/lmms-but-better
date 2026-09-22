/* VST3 host regression and installed-plugin integration tests.
 * Copyright (c) 2026 LMMS developers. Licensed under GPL-2.0-or-later.
 */
#include <QtTest>
#include <QDomDocument>
#include <QPushButton>
#include <QPainter>
#include <QMenu>
#include <QProcess>
#include <QToolButton>
#ifndef Q_OS_MACOS
#include <xcb/xcb.h>
#endif
#include <cstdlib>
#include <cmath>

#include "AudioEngine.h"
#include "AudioDevice.h"
#include "AudioBuffer.h"
#include "DetuningHelper.h"
#include "Effect.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "Song.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "MidiEvent.h"
#include "MidiClip.h"
#include "PianoRoll.h"
#include "ProjectJournal.h"
#include "MainApplication.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "AudioDummy.h"
#include "SampleFrame.h"
#include "Vst3HostApp.h"
#include "Vst3Manager.h"
#include "Vst3Module.h"
#include "Vst3Plugin.h"
#include "Vst3ViewBase.h"

using namespace lmms;
using namespace lmms::vst3;

namespace
{

// QScreen::grabWindow captures the root window, which is black on XWayland.
// Read the editor's own X11 drawables, including its embedded child windows.
#ifndef Q_OS_MACOS
QImage captureEditor(xcb_connection_t* connection, xcb_window_t window)
{
	auto geometry = xcb_get_geometry_reply(connection, xcb_get_geometry(connection, window), nullptr);
	if (!geometry) { return {}; }
	const auto width = geometry->width;
	const auto height = geometry->height;
	std::free(geometry);
	if (!width || !height) { return {}; }
	auto reply = xcb_get_image_reply(connection,
		xcb_get_image(connection, XCB_IMAGE_FORMAT_Z_PIXMAP, window, 0, 0, width, height, ~0u), nullptr);
	if (!reply) { return {}; }
	const int stride = xcb_get_image_data_length(reply) / height;
	QImage result;
	if (stride >= width * 4)
	{
		result = QImage(xcb_get_image_data(reply), width, height, stride, QImage::Format_RGB32).copy();
	}
	std::free(reply);
	if (result.isNull()) { return result; }
	auto tree = xcb_query_tree_reply(connection, xcb_query_tree(connection, window), nullptr);
	if (!tree) { return result; }
	QPainter painter{&result};
	for (int i = 0; i < xcb_query_tree_children_length(tree); ++i)
	{
		const auto child = xcb_query_tree_children(tree)[i];
		auto attributes = xcb_get_window_attributes_reply(connection, xcb_get_window_attributes(connection, child), nullptr);
		const bool visible = attributes && attributes->map_state == XCB_MAP_STATE_VIEWABLE;
		std::free(attributes);
		if (!visible) { continue; }
		auto position = xcb_get_geometry_reply(connection, xcb_get_geometry(connection, child), nullptr);
		if (position) { painter.drawImage(position->x, position->y, captureEditor(connection, child)); }
		std::free(position);
	}
	std::free(tree);
	return result;
}

#endif

} // namespace

class Vst3HostTest : public QObject
{
	Q_OBJECT
	QTemporaryDir m_configDir;
	std::unique_ptr<gui::GuiApplication> m_gui;
private slots:
	void initTestCase()
	{
		NotePlayHandleManager::init();
		QVERIFY(m_configDir.isValid());
		const QString config = m_configDir.path() + "/lmmsrc.xml";
		const QString userConfig = QDir::homePath() + "/.lmmsrc.xml";
		if (QFileInfo::exists(userConfig)) { QVERIFY(QFile::copy(userConfig, config)); }
		ConfigManager::inst()->loadConfigFile(config);
		if (qEnvironmentVariableIsSet("LMMS_TEST_VST3_GUI") || qEnvironmentVariableIsSet("LMMS_TEST_GLIDE_GUI"))
		{
			// Generic controls depend on LMMS's GUI context, so exercise
			// the real application startup and panel instead of just IPlugView.
			ConfigManager::inst()->setValue("audioengine", "audiodev", AudioDummy::name());
			ConfigManager::inst()->setValue("ui", "enableautosave", "0");
			m_gui = std::make_unique<gui::GuiApplication>();
		}
	}

	void nativeArchitectureOnly()
	{
		QTemporaryDir dir;
		const QString bundle = dir.path() + "/Example.vst3";
		QDir{}.mkpath(bundle + "/Contents/unsupported-linux");
		QFile binary{bundle + "/Contents/unsupported-linux/Example.so"};
		QVERIFY(binary.open(QIODevice::WriteOnly));
		binary.write("not a native module");
		binary.close();
		QVERIFY(Vst3Module::resolveModulePath(bundle).isEmpty());
		QString error;
		QVERIFY(!Vst3Module::open(bundle, &error));
		QVERIFY(!error.isEmpty());
	}

	void searchPathsIncludeUserPlugins()
	{
		const auto paths = Vst3Manager::searchPaths();
#ifdef Q_OS_MACOS
		QVERIFY(paths.contains(QDir::homePath() + "/Library/Audio/Plug-Ins/VST3"));
		QVERIFY(paths.contains("/Library/Audio/Plug-Ins/VST3"));
#else
		QVERIFY(paths.contains(QDir::homePath() + "/.vst3"));
#endif
		QVERIFY(paths.contains(ConfigManager::inst()->vstDir()));
	}

	void stateStreamRoundTrip()
	{
		MemoryStream stream;
		QByteArray data("state\0with binary", 17);
		Steinberg::int32 count = 0;
		QCOMPARE(stream.write(data.data(), data.size(), &count), Steinberg::kResultOk);
		QCOMPARE(count, data.size());
		stream.rewind();
		QByteArray result(data.size(), '\0');
		QCOMPARE(stream.read(result.data(), result.size(), &count), Steinberg::kResultOk);
		QCOMPARE(result, data);
		QCOMPARE(stream.seek(-1, Steinberg::IBStream::kIBSeekSet, nullptr), Steinberg::kInvalidArgument);
	}

	void cleanupTestCase()
	{
		if (m_gui)
		{
			m_gui->mainWindow()->deleteLater(); // also destroys the engine
			QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
			m_gui.reset();
		}
		else if (Engine::audioEngine()) { Engine::destroy(); }
		NotePlayHandleManager::free();
	}

	void scannerSurvivesCrash()
	{
		QTemporaryDir dir;
#ifdef Q_OS_MACOS
		const QString crashing = dir.path() + "/crashing.vst3";
		QVERIFY(QDir{}.mkpath(crashing + "/Contents/MacOS"));
		const QString source = Vst3Module::hostPath(VST3_TEST_MODULE);
		QVERIFY(QFile::copy(source + "/Contents/Info.plist", crashing + "/Contents/Info.plist"));
		QVERIFY(QFile::copy(VST3_TEST_MODULE, crashing + "/Contents/MacOS/" + QFileInfo{VST3_TEST_MODULE}.fileName()));
#else
		const QString crashing = dir.path() + "/crashing.so";
		QVERIFY(QFile::copy(VST3_TEST_MODULE, crashing));
#endif
		qputenv("LMMS_TEST_VST3_CRASH", "1");
		const auto result = Vst3Manager::instance()->classesInFile(crashing);
		qunsetenv("LMMS_TEST_VST3_CRASH");
		QVERIFY(result.empty());
		QCOMPARE(Vst3Manager::instance()->classesInFile(VST3_TEST_MODULE).size(), std::size_t{1});
	}

	void installedDiscovery()
	{
		if (!qEnvironmentVariableIsSet("LMMS_TEST_VST3_SCAN")) { QSKIP("Set LMMS_TEST_VST3_SCAN to scan installed plugins"); }
		const auto& classes = Vst3Manager::instance()->classes();
		QVERIFY(!classes.empty());
		for (const auto& info : classes) { qInfo() << info.name << info.modulePath; }
	}

	void rejectsDamagedSignature()
	{
#ifdef Q_OS_MACOS
		QTemporaryDir dir;
		const QString bundle = dir.path() + "/damaged.vst3";
		QVERIFY(QDir{}.mkpath(bundle + "/Contents/MacOS"));
		const QString source = Vst3Module::hostPath(VST3_TEST_MODULE);
		const QString binary = bundle + "/Contents/MacOS/" + QFileInfo{VST3_TEST_MODULE}.fileName();
		QVERIFY(QFile::copy(source + "/Contents/Info.plist", bundle + "/Contents/Info.plist"));
		QVERIFY(QFile::copy(VST3_TEST_MODULE, binary));
		QCOMPARE(QProcess::execute("/usr/bin/codesign", {"--force", "--sign", "-", bundle}), 0);
		// Damage a signed executable page without changing the Mach-O layout.
		QFile file{binary};
		QVERIFY(file.open(QIODevice::ReadWrite));
		const auto offset = file.readAll().indexOf("LMMS VST3 test gain");
		QVERIFY(offset >= 0);
		QVERIFY(file.seek(offset));
		QCOMPARE(file.write("X", 1), qint64{1});
		file.close();
		QString error;
		QVERIFY(!Vst3Module::open(bundle, &error));
		QVERIFY2(error.contains("code signature validation failed"), qPrintable(error));
		QVERIFY(error.contains("Reinstall"));
		// The scanner uses the same check and must not execute the bundle.
		QVERIFY(Vst3Manager::instance()->classesInFile(bundle).empty());
#else
		QSKIP("macOS code signature validation");
#endif
	}

	void installedInvalidSignature()
	{
		const auto path = qEnvironmentVariable("LMMS_TEST_VST3_INVALID_SIGNATURE");
		if (path.isEmpty()) { QSKIP("Set LMMS_TEST_VST3_INVALID_SIGNATURE to a damaged signed plugin"); }
		QString error;
		QVERIFY(!Vst3Module::open(path, &error));
		QVERIFY2(error.contains("code signature validation failed"), qPrintable(error));
		qInfo().noquote() << error;
	}

	void deterministicAudioAndMidi()
	{
		if (!Engine::audioEngine()) { Engine::init(true); }
		{
			const auto classes = Vst3Manager::instance()->classesInFile(VST3_TEST_MODULE);
			QCOMPARE(classes.size(), std::size_t{1});
			Model parent{nullptr};
			Vst3Plugin plugin{&parent, classes[0].uid, VST3_TEST_MODULE};
			std::vector<SampleFrame> input(32, SampleFrame{1.f, .5f}), output(32);
			plugin.process(input.data(), output.data(), 32);
			QCOMPARE(output[0].left(), .5f);
			QCOMPARE(output[0].right(), .25f);
			plugin.param(0)->model->setValue(0.f);
			plugin.process(input.data(), output.data(), 32);
			QCOMPARE(output[0].left(), 0.f);
			QCOMPARE(output[0].right(), 0.f);
			plugin.param(0)->model->setValue(.5f);
			plugin.handleMidiInputEvent(MidiEvent{MidiEventTypes::MidiNoteOn, 0, 60, 100}, {}, 8);
			plugin.handleMidiInputEvent(MidiEvent{MidiEventTypes::MidiNoteOff, 0, 60, 0}, {}, 16);
			plugin.process(nullptr, output.data(), 32);
			QCOMPARE(output[7].left(), 0.f);
			QCOMPARE(output[8].left(), .125f);
			QCOMPARE(output[15].left(), .125f);
			QCOMPARE(output[16].left(), 0.f);
			plugin.param(0)->model->setValue(.37f);
			QDomDocument doc;
			auto state = doc.createElement("vst3");
			plugin.saveSettings(doc, state);
			state.removeAttribute("chunk");
			state.removeAttribute("ctrlchunk");
			plugin.param(0)->model->setValue(.81f);
			plugin.loadSettings(state);
			QCOMPARE(plugin.param(0)->model->value(), .37f);
			// Returning to the default before the next process call must
			// also be persisted, even though it equals the default value.
			plugin.param(0)->model->setValue(.5f);
			auto defaultState = doc.createElement("vst3");
			plugin.saveSettings(doc, defaultState);
			QVERIFY(defaultState.firstChildElement("params").hasAttribute("param7"));
		}

	}

	void lmmsInstrumentAndEffect()
	{
#ifndef LMMS_TEST_VST3_WRAPPERS
		QSKIP("Build Vst3Instrument and Vst3Effect to test the LMMS wrappers");
#endif
		if (!Engine::audioEngine()) { Engine::init(true); }
		auto guard = Engine::audioEngine()->requestChangesGuard();
		const auto classes = Vst3Manager::instance()->classesInFile(VST3_TEST_MODULE);
		QCOMPARE(classes.size(), std::size_t{1});
		using Key = Plugin::Descriptor::SubPluginFeatures::Key;
		Key key{nullptr, classes[0].name, {{"uid", classes[0].uid}, {"file", VST3_TEST_MODULE}}};
		Model parent{nullptr};
		std::unique_ptr<Effect> effect{Effect::instantiate("vst3effect", &parent, &key)};
		QVERIFY(effect && effect->isOkay());
		AudioBuffer buffer{32};
		buffer.allocateInterleavedBuffer();
		for (auto& sample : buffer.interleavedBuffer().asSampleFrames()) { sample = SampleFrame{1.f, .5f}; }
		std::fill(buffer.buffer(0).begin(), buffer.buffer(0).end(), 1.f);
		std::fill(buffer.buffer(1).begin(), buffer.buffer(1).end(), .5f);
		buffer.updateSilenceFlags(0b11);
		effect->processAudioBuffer(buffer);
		QCOMPARE(buffer.buffer(0)[0], .5f);
		QCOMPARE(buffer.buffer(1)[0], .25f);
		InstrumentTrack track{Engine::getSong()};
		auto instrument = track.loadInstrument("vst3instrument");
		QVERIFY(instrument);
		QCOMPARE(QString::fromUtf8(instrument->descriptor()->name), QString{"vst3instrument"});
		instrument->loadFile(VST3_TEST_MODULE);
		instrument->handleMidiEvent(MidiEvent{MidiEventTypes::MidiNoteOn, 0, 60, 100}, {}, 0);
		std::vector<SampleFrame> output(Engine::audioEngine()->framesPerPeriod());
		instrument->play(output.data());
		QCOMPARE(output[0].left(), .125f);
		instrument->handleMidiEvent(MidiEvent{MidiEventTypes::MidiNoteOn, 0, 60, 0}, {}, 0);
		instrument->play(output.data());
		QCOMPARE(output[0].left(), 0.f);
	}

	void noteGlideThroughInstrument()
	{
#ifndef LMMS_TEST_VST3_WRAPPERS
		QSKIP("Build Vst3Instrument to test note glide routing");
#endif
		if (!Engine::audioEngine()) { Engine::init(true); }
		auto guard = Engine::audioEngine()->requestChangesGuard();
		InstrumentTrack track{Engine::getSong()};
		auto* instrument = track.loadInstrument("vst3instrument");
		QVERIFY(instrument);
		instrument->loadFile(VST3_TEST_MODULE);
		track.pitchRangeModel()->setValue(12);
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		std::vector<SampleFrame> output(frames);
		Note note{TimePos{192}, TimePos{0}, 60};
		note.createDetuning();
		note.detuning()->automationClip()->putValue(0, 6, false);
		note.detuning()->automationClip()->putValue(100, 12, false);
		using Handle = std::unique_ptr<NotePlayHandle, decltype(&NotePlayHandleManager::release)>;
		Handle handle{NotePlayHandleManager::acquire(&track, 8, frames * 1000, note), &NotePlayHandleManager::release};
		handle->play(nullptr);
		instrument->play(output.data());
		QCOMPARE(output[7].left(), 0.f);
		auto expected = [](float semitones)
		{
			return float(.125 + (MidiNotePitch::bend(semitones, 12) - 8192.) / 16383.);
		};
		QCOMPARE(output[8].left(), expected(6));
		// The curve update must retain its frame offset inside the audio period.
		handle->processTimePos(50, 0, false, 16);
		instrument->play(output.data());
		QCOMPARE(output[15].left(), expected(6));
		const float curvePitch = note.detuning()->automationClip()->valueAt(50);
		QCOMPARE(output[16].left(), expected(curvePitch));
		// Track pitch is added to the curve instead of replacing it.
		track.pitchModel()->setValue(100);
		instrument->play(output.data());
		QCOMPARE(output[0].left(), expected(curvePitch + 1));
		handle->noteOff(16);
		Note plain{TimePos{192}, TimePos{0}, 64};
		Handle plainHandle{NotePlayHandleManager::acquire(&track, 24, frames * 1000, plain), &NotePlayHandleManager::release};
		plainHandle->play(nullptr);
		instrument->play(output.data());
		QCOMPARE(output[15].left(), expected(curvePitch + 1));
		QCOMPARE(output[16].left(), 0.f);
		QCOMPARE(output[24].left(), expected(1));
	}

	void glideEditor()
	{
		if (!m_gui) { QSKIP("Set LMMS_TEST_GLIDE_GUI to exercise the glide editor"); }
		auto guard = Engine::audioEngine()->requestChangesGuard();
		// Keep the fixture out of the Song Editor's asynchronous track views.
		struct TestContainer : TrackContainer
		{
			QString nodeName() const override { return "trackcontainer"; }
		} container;
		InstrumentTrack track{&container};
		track.setName("Glide curve");
		MidiClip clip{&track};
		clip.clearNotes();
		auto* note = clip.addNote(Note{TimePos{384}, TimePos{0}, 60}, false);
		note->createDetuning();
		auto* curve = note->detuning()->automationClip();
		curve->putValue(0, 0, false);
		curve->putValue(383, 12, false);
		QCOMPARE(curve->progressionType(), AutomationClip::ProgressionType::CubicHermite);
		QVERIFY(curve->setNodeTangent(0, true, 0, true));
		QVERIFY(curve->setNodeTangent(383, false, 0, true));
		QVERIFY(curve->getTimeMap().first().lockedTangents());
		QCOMPARE(curve->getTimeMap().first().getInTangent(), curve->getTimeMap().first().getOutTangent());
		QVERIFY(curve->valueAt(96) < 2.f);
		auto* window = m_gui->pianoRoll();
		window->setCurrentMidiClip(&clip);
		note->setSelected(true);
		window->resize(1100, 640);
		window->show();
		for (auto* action : window->findChildren<QAction*>())
		{
			if (action->text() == "Pitch Bend mode (Shift+T)") { action->trigger(); break; }
		}
		auto* button = window->findChild<QToolButton*>("pianoRollGlideMenuButton");
		QVERIFY(button);
		auto* menu = button->menu();
		QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShow", Qt::DirectConnection));
		auto trigger = [menu](const QString& text)
		{
			for (auto* action : menu->actions())
			{
				if (action->text() == text) { action->trigger(); return true; }
			}
			return false;
		};
		QCOMPARE(menu->actions().size(), 3);
		QCOMPARE(menu->actions().front()->text(), QString{"Reset selected curves"});
		QCOMPARE(menu->actions().back()->text(), QString{"External synth bend range…"});
		QDomDocument doc;
		auto parent = doc.createElement("notes");
		auto saved = note->saveState(doc, parent);
		Note restored;
		restored.restoreState(saved);
		QVERIFY(restored.hasDetuningInfo());
		QCOMPARE(restored.detuning()->automationClip()->valueAt(96), curve->valueAt(96));
		const auto screenshot = qEnvironmentVariable("LMMS_TEST_GLIDE_SCREENSHOT");
		if (!screenshot.isEmpty())
		{
			QCoreApplication::processEvents();
			QVERIFY(window->grab().save(screenshot));
		}
		QVERIFY(trigger("Reset selected curves"));
		QVERIFY(!note->hasDetuningInfo());
		Engine::projectJournal()->undo();
		note = clip.notes().front();
		QVERIFY(note->hasDetuningInfo());
		QCOMPARE(note->detuning()->automationClip()->getTimeMap().size(), 2);
		window->setCurrentMidiClip(nullptr);
	}

	void installedVitalGlide()
	{
#ifndef LMMS_TEST_VST3_WRAPPERS
		QSKIP("Build Vst3Instrument to test Vital glides");
#endif
		const QString path = qEnvironmentVariable("LMMS_TEST_VITAL");
		if (path.isEmpty()) { QSKIP("Set LMMS_TEST_VITAL to a Vital VST3 bundle"); }
		if (!Engine::audioEngine()) { Engine::init(true); }
		auto guard = Engine::audioEngine()->requestChangesGuard();
		InstrumentTrack track{Engine::getSong()};
		auto* instrument = track.loadInstrument("vst3instrument");
		QVERIFY(instrument);
		instrument->loadFile(path);
		auto* plugin = instrument->findChild<Vst3Plugin*>();
		QVERIFY(plugin);
		// Use Vital's initialized patch and discover its configured bend range.
		bool foundRange = false;
		for (std::size_t i = 0; i < plugin->paramCount(); ++i)
		{
			auto* param = plugin->param(i);
			if (!param->title.contains("bend", Qt::CaseInsensitive)) { continue; }
			qInfo() << "Vital bend parameter:" << param->title << param->model->value();
			// Vital's pitch_bend_range parameter covers 0–48 semitones.
			if (param->title.contains("range", Qt::CaseInsensitive))
			{
				param->model->setValue(12.f / 48.f);
				foundRange = true;
			}
		}
		QVERIFY(foundRange);
		track.pitchRangeModel()->setValue(12);
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		std::vector<SampleFrame> output(frames);
		Note note{TimePos{192}, TimePos{0}, 60};
		note.createDetuning();
		note.detuning()->automationClip()->putValue(0, 0, false);
		note.detuning()->automationClip()->putValue(100, 12, false);
		using Handle = std::unique_ptr<NotePlayHandle, decltype(&NotePlayHandleManager::release)>;
		Handle handle{NotePlayHandleManager::acquire(&track, 0, frames * 1000, note), &NotePlayHandleManager::release};
		handle->play(nullptr);
		auto frequency = [&]
		{
			// Discard parameter smoothing and attack, then estimate the period by
			// autocorrelation. Wavetables can cross zero several times per cycle.
			for (int i = 0; i < 40; ++i) { instrument->play(output.data()); }
			std::vector<float> samples;
			while (samples.size() < 8192)
			{
				instrument->play(output.data());
				for (const auto& sample : output) { samples.push_back(sample.left()); }
			}
			const double rate = Engine::audioEngine()->outputSampleRate();
			std::vector<double> errors(int(rate / 150) + 2);
			for (int lag = 1; lag < int(errors.size()); ++lag)
			{
				double difference = 0, energy = 0;
				for (int i = 0; i < 4096; ++i)
				{
					const double a = samples[i], b = samples[i + lag];
					difference += (a - b) * (a - b);
					energy += a * a + b * b;
				}
				errors[lag] = energy > 1.e-9 ? difference / energy : 1.;
			}
			for (int lag = int(rate / 1000); lag + 1 < int(errors.size()); ++lag)
			{
				if (errors[lag] < .05 && errors[lag] < errors[lag - 1] && errors[lag] < errors[lag + 1])
				{
					return rate / lag;
				}
			}
			return 0.;
		};
		const double base = frequency();
		handle->processTimePos(100, 0, false);
		const double bent = frequency();
		qInfo() << "Vital glide frequencies:" << base << bent;
		QVERIFY(base > 200 && base < 300);
		QVERIFY(std::abs(bent / base - 2.) < .03);
	}

	void installedNativeEditor()
	{
		if (!qEnvironmentVariableIsSet("LMMS_TEST_VST3_NATIVE_GUI"))
		{
			QSKIP("Set LMMS_TEST_VST3_NATIVE_GUI and LMMS_TEST_VST3 to test the native editor");
		}
		const auto path = qEnvironmentVariable("LMMS_TEST_VST3");
		QVERIFY(!path.isEmpty());
		if (!Engine::audioEngine()) { Engine::init(true); }
		const auto classes = Vst3Manager::instance()->classesInFile(path);
		QVERIFY(!classes.empty());
		Model parent{nullptr};
		Vst3Plugin plugin{&parent, classes.front().uid, path};
		gui::Vst3EditorWindow editor{&plugin};
		for (int pass = 0; pass < 2; ++pass)
		{
			QVERIFY(editor.attachView());
			editor.show();
			QTRY_VERIFY_WITH_TIMEOUT(editor.isViewAttached(), 5000);
			QVERIFY(editor.isVisible());
			QVERIFY(editor.width() > 0 && editor.height() > 0);
			editor.move(editor.pos() + QPoint{30, 30});
			QTest::qWait(1000);
			editor.close();
			QVERIFY(!editor.isViewAttached());
			QVERIFY(!editor.isVisible());
		}
	}

	void installedPlugin()
	{
		const QString path = qEnvironmentVariable("LMMS_TEST_VST3");
		if (path.isEmpty()) { QSKIP("Set LMMS_TEST_VST3 to a native or yabridged VST3 plugin"); }
		if (!Engine::audioEngine()) { Engine::init(true); }
		const auto classes = Vst3Manager::instance()->classesInFile(path);
		QVERIFY2(!classes.empty(), qPrintable(path));
		for (const auto& info : classes)
		{
			Model parent{nullptr};
			Vst3Plugin plugin{&parent, info.uid, info.modulePath};
			qInfo() << "Testing" << info.name << info.modulePath << "parameters:" << plugin.paramCount();
			const auto frames = Engine::audioEngine()->framesPerPeriod();
			std::vector<SampleFrame> input(frames), output(frames);
			plugin.handleMidiInputEvent(MidiEvent{MidiEventTypes::MidiNoteOn, 0, 60, 100}, {}, 0);
			double energy = 0;
			for (int block = 0; block < 180; ++block)
			{
				for (unsigned f = 0; f < frames; ++f)
				{
					const float value = .2f * std::sin((block * frames + f) * .05);
					input[f] = SampleFrame{value, value};
				}
				plugin.process(info.isInstrument ? nullptr : input.data(), output.data(), frames);
				for (const auto& sample : output)
				{
					QVERIFY(std::isfinite(sample.left()) && std::isfinite(sample.right()));
					energy += sample.left() * sample.left() + sample.right() * sample.right();
				}
			}
			qInfo() << "Output energy:" << energy;
			QVERIFY2(energy > 1.e-6, "Plugin produced no audio");
			plugin.handleMidiInputEvent(MidiEvent{MidiEventTypes::MidiNoteOff, 0, 60, 0}, {}, 0);
			plugin.process(nullptr, output.data(), frames);

			// Values saved as XML attributes must survive, even without a chunk.
			if (plugin.paramCount())
			{
				auto param = plugin.param(0);
				param->model->setValue(.37f);
				const float saved = param->model->value();
				QDomDocument doc;
				auto state = doc.createElement("vst3");
				plugin.saveSettings(doc, state);
				QVERIFY(state.hasAttribute("chunk"));
				param->model->setValue(.81f);
				plugin.loadSettings(state);
				QCOMPARE(param->model->value(), saved);
				state.removeAttribute("chunk");
				state.removeAttribute("ctrlchunk");
				param->model->setValue(.81f);
				plugin.loadSettings(state);
				QCOMPARE(param->model->value(), saved);
				Vst3Plugin restored{&parent, info.uid, info.modulePath};
				restored.loadSettings(state);
				QCOMPARE(restored.param(0)->model->value(), saved);
			}
			plugin.reconfigure();
			plugin.process(input.data(), output.data(), frames);
			if (qEnvironmentVariableIsSet("LMMS_TEST_VST3_GUI"))
			{
				qInfo() << "Editor platform:" << QGuiApplication::platformName();
				gui::Vst3PluginWidget panel{&plugin, nullptr};
				panel.show();
				auto button = panel.findChild<QPushButton*>("vst3ShowEditor");
				QVERIFY(button);
				QTest::mouseClick(button, Qt::LeftButton);
				gui::Vst3EditorWindow* editor = nullptr;
				for (auto window : QApplication::topLevelWidgets())
				{
					if (auto candidate = qobject_cast<gui::Vst3EditorWindow*>(window)) { editor = candidate; }
				}
				QVERIFY(editor);
				QTRY_VERIFY_WITH_TIMEOUT(editor->isViewAttached(), 5000);
				QVERIFY(editor->isVisible());
				QVERIFY(button->isChecked());
				QTest::qWait(1000);
				const auto screenshot = qEnvironmentVariable("LMMS_TEST_VST3_SCREENSHOT");
				if (!screenshot.isEmpty())
				{
#ifndef Q_OS_MACOS
					auto connection = xcb_connect(nullptr, nullptr);
					const auto image = captureEditor(connection, static_cast<xcb_window_t>(editor->winId()));
					xcb_disconnect(connection);
#else
					const auto image = editor->grab().toImage();
#endif
					QVERIFY(!image.isNull());
					QVERIFY(image.save(screenshot));
				}
				QTest::mouseClick(button, Qt::LeftButton);
				QVERIFY(!editor->isVisible());
				QVERIFY(!editor->isViewAttached());
				QTest::mouseClick(button, Qt::LeftButton);
				QTRY_VERIFY_WITH_TIMEOUT(editor->isViewAttached(), 5000);
				editor->close();
				QVERIFY(!button->isChecked());
			}
		}

	}
};
int main(int argc, char** argv)
{
	lmms::gui::MainApplication app{argc, argv};
	Vst3HostTest test;
	return QTest::qExec(&test, argc, argv);
}
#include "Vst3HostTest.moc"
