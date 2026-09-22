/*
 * PluginSnapshot.cpp - developer tool: renders native plugin UIs to PNG files and
 *                      runs a signal through effects to check for invalid output.
 *
 * Usage: PluginSnapshot <outdir> effect:<name>[:param=value,...] instrument:<name> ...
 * Run with QT_QPA_PLATFORM=offscreen, LMMS_PLUGIN_DIR and LMMS_DATA_DIR set.
 */

#include <QApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QDomDocument>
#include <QTextStream>
#include <QTimer>
#include <QWidget>
#include <cmath>
#include <cstdio>
#include <vector>

// processImpl() is protected; this tool drives effects directly without an effect chain
#define protected public
#include "Effect.h"
#undef protected
#include "AudioDummy.h"
#include "AudioEngine.h"
#include "ConfigManager.h"
#include "GuiApplication.h"
#include "MainApplication.h"
#include "NotePlayHandle.h"
#include "EffectControls.h"
#include "EffectControlDialog.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "InstrumentView.h"
#include "InstrumentTrackView.h"
#include "InstrumentTrackWindow.h"
#include "NotePlayHandle.h"
#include "SampleFrame.h"
#include "Song.h"

using namespace lmms;

namespace
{

//! Apply "name=value" pairs to the effect's controls by XML round-trip
void applyParams(Effect* effect, const QStringList& params)
{
	if (params.isEmpty()) { return; }
	QDomDocument doc;
	auto element = doc.createElement("controls");
	effect->controls()->saveSettings(doc, element);
	for (const auto& param : params)
	{
		// "@Display name=value" sets a model directly, for transient (unsaved) models
		if (param.startsWith('@'))
		{
			const auto kv = param.mid(1).split('=');
			for (auto model : effect->controls()->findChildren<AutomatableModel*>())
			{
				if (model->displayName() == kv.value(0)) { model->setValue(kv.value(1).toFloat()); }
			}
			continue;
		}
		const auto kv = param.split('=');
		if (kv.size() != 2) { continue; }
		// Values might be stored as attributes or as child elements
		auto child = element.firstChildElement(kv[0]);
		if (!child.isNull()) { child.setAttribute("value", kv[1]); }
		else { element.setAttribute(kv[0], kv[1]); }
	}
	// Re-apply direct settings after loading, which would otherwise reset them
	effect->controls()->loadSettings(element);
	for (const auto& param : params)
	{
		if (!param.startsWith('@')) { continue; }
		const auto kv = param.mid(1).split('=');
		for (auto model : effect->controls()->findChildren<AutomatableModel*>())
		{
			if (model->displayName() == kv.value(0)) { model->setValue(kv.value(1).toFloat()); }
		}
	}
}

bool runEffect(Effect* effect, const QString& name)
{
	const auto frames = Engine::audioEngine()->framesPerPeriod();
	const float sr = Engine::audioEngine()->outputSampleRate();
	std::vector<SampleFrame> buf(frames);
	double inEnergy = 0, outEnergy = 0;
	float peak = 0;
	const int blocks = static_cast<int>(3.0 * sr / frames);
	for (int b = 0; b < blocks; ++b)
	{
		for (f_cnt_t f = 0; f < frames; ++f)
		{
			const double t = (b * frames + f) / sr;
			// Decaying plucks every 0.5 s during the first 1.5 s, then silence to hear tails
			// ...and a steady tone in the last 0.3 s so level displays have something to show
			const double env = t < 1.5 ? std::exp(-std::fmod(t, 0.5) * 12.0) : (t > 2.7 ? 0.6 : 0.0);
			const float l = static_cast<float>(0.5 * env * std::sin(2 * M_PI * 220 * t));
			const float r = static_cast<float>(0.5 * env * std::sin(2 * M_PI * 330 * t));
			buf[f] = SampleFrame(l, r);
			inEnergy += l * l + r * r;
		}
		effect->processImpl(buf.data(), frames);
		for (const auto& s : buf)
		{
			if (!std::isfinite(s.left()) || !std::isfinite(s.right()))
			{
				std::printf("FAIL %s: non-finite output in block %d\n", qPrintable(name), b);
				return false;
			}
			outEnergy += s.left() * s.left() + s.right() * s.right();
			peak = std::max({peak, std::abs(s.left()), std::abs(s.right())});
		}
	}
	std::printf("OK   %-22s in=%.2f out=%.2f peak=%.3f\n", qPrintable(name), inEnergy, outEnergy, peak);
	return peak < 20.f;
}

void applyInstrumentParams(Instrument* instrument, const QStringList& params)
{
	if (params.isEmpty()) { return; }
	QDomDocument doc;
	auto element = doc.createElement("instrument");
	instrument->saveSettings(doc, element);
	for (const auto& param : params)
	{
		const auto kv = param.split('=');
		if (kv.size() != 2) { continue; }
		auto child = element.firstChildElement(kv[0]);
		// A value of "!" removes the setting, simulating a project saved by an older version
		if (kv[1] == "!") { element.removeAttribute(kv[0]); if (!child.isNull()) { element.removeChild(child); } }
		else if (!child.isNull()) { child.setAttribute("value", kv[1]); }
		else { element.setAttribute(kv[0], kv[1]); }
	}
	instrument->loadSettings(element);
}

//! Plays a note through the instrument (bypassing the audio engine thread) and checks the output
bool runInstrument(InstrumentTrack* track, const QString& name)
{
	auto instrument = track->instrument();
	const auto frames = Engine::audioEngine()->framesPerPeriod();
	const float sr = Engine::audioEngine()->outputSampleRate();
	std::vector<SampleFrame> buf(frames);
	auto note = NotePlayHandleManager::acquire(track, 0, static_cast<f_cnt_t>(sr), Note(TimePos(0), TimePos(0), 57, 100));
	double energy = 0, sideEnergy = 0;
	float peak = 0;
	// With PLUGINSNAPSHOT_PITCH set, print the pitch (from zero crossings) of each 1/16 note at the song tempo
	const bool printPitch = qEnvironmentVariableIsSet("PLUGINSNAPSHOT_PITCH");
	const double sixteenth = sr * 60.0 / Engine::getSong()->getTempo() / 4.0;
	std::vector<float> history;
	for (int b = 0; b < static_cast<int>((printPitch ? 1.8f : 0.5f) * sr / frames); ++b)
	{
		zeroSampleFrames(buf.data(), frames);
		instrument->playNote(note, buf.data());
		// Single-streamed instruments (e.g. LB302) only queue notes in playNote() and render in play()
		if (instrument->isSingleStreamed()) { instrument->play(buf.data()); }
		for (const auto& s : buf)
		{
			if (!std::isfinite(s.left()) || !std::isfinite(s.right()))
			{
				std::printf("FAIL %s: non-finite output\n", qPrintable(name));
				return false;
			}
			energy += s.left() * s.left() + s.right() * s.right();
			const float side = s.left() - s.right();
			sideEnergy += side * side;
			peak = std::max({peak, std::abs(s.left()), std::abs(s.right())});
			if (printPitch) { history.push_back(s.left()); }
		}
	}
	if (printPitch)
	{
		std::printf("     pitch per 1/16:");
		for (double start = 0; start + sixteenth <= history.size(); start += sixteenth)
		{
			// Only the first half of each step: plain notes are gated at 55 %
			int crossings = 0;
			const auto from = static_cast<std::size_t>(start + sixteenth * 0.1);
			const auto to = static_cast<std::size_t>(start + sixteenth * 0.5);
			for (auto i = from + 1; i < to; ++i) { if (history[i - 1] <= 0 && history[i] > 0) { ++crossings; } }
			std::printf(" %.0f", crossings / ((to - from) / sr));
		}
		std::printf("\n");
	}
	instrument->deleteNotePluginData(note);
	note->m_pluginData = nullptr;
	std::printf("OK   %-22s energy=%.2f side=%.2f peak=%.3f\n", qPrintable(name), energy, sideEnergy, peak);
	return energy > 1e-3 && peak < 20.f;
}

void save(QWidget* widget, const QString& path)
{
	widget->adjustSize();
	widget->show();
	// Let timer-driven displays (meters, scopes, level indicators) catch up
	QElapsedTimer timer;
	timer.start();
	while (timer.elapsed() < 150) { QApplication::processEvents(QEventLoop::AllEvents, 20); }
	widget->grab().save(path);
	std::printf("     saved %s (%dx%d)\n", qPrintable(path), widget->width(), widget->height());
}

} // namespace

int main(int argc, char** argv)
{
	gui::MainApplication app(argc, argv);
	if (argc < 3)
	{
		std::printf("usage: %s <outdir> effect:<name>[:k=v,...] instrument:<name>\n", argv[0]);
		return 1;
	}
	// Knobs and other widgets need the real GUI context (main window for tool tips etc.)
	NotePlayHandleManager::init();
	QTemporaryDir configDir;
	ConfigManager::inst()->loadConfigFile(configDir.path() + "/lmmsrc.xml");
	ConfigManager::inst()->setValue("audioengine", "audiodev", AudioDummy::name());
	ConfigManager::inst()->setValue("ui", "enableautosave", "0");
	ConfigManager::inst()->setValue("app", "configured", "1");
	auto guiApp = new gui::GuiApplication();
	Q_UNUSED(guiApp);
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	const QDir outDir(argv[1]);
	int failures = 0;

	for (int i = 2; i < argc; ++i)
	{
		const auto spec = QString(argv[i]).split(':');
		const auto kind = spec.value(0);
		const auto name = spec.value(1);
		const auto params = spec.size() > 2 ? spec[2].split(',') : QStringList{};
		auto tag = params.isEmpty() ? name : name + "_" + QString(params.join("_")).replace('=', '-');
		// Keep only the file name of path parameters so the tag is a valid file name
		tag = tag.replace(QRegularExpression(R"([^_\-]*/)"), "").replace(QRegularExpression(R"([^A-Za-z0-9_.\-])"), "").left(80);

		if (kind == "effect")
		{
			Model parent(nullptr);
			auto effect = Effect::instantiate(name, &parent, nullptr);
			if (!effect || effect->descriptor()->name != name)
			{
				std::printf("FAIL %s: could not instantiate\n", qPrintable(name));
				++failures;
				continue;
			}
			applyParams(effect, params);
			if (!runEffect(effect, tag)) { ++failures; }
			auto view = effect->controls()->createView();
			save(view, outDir.filePath(tag + ".png"));
			delete view;
			delete effect;
		}
		else if (kind == "legacy")
		{
			// Load settings that only contain the given attributes (like an old project) and dump the result
			Model parent(nullptr);
			auto effect = Effect::instantiate(name, &parent, nullptr);
			QDomDocument doc;
			auto element = doc.createElement("controls");
			for (const auto& param : params)
			{
				const auto kv = param.split('=');
				if (kv.size() == 2) { element.setAttribute(kv[0], kv[1]); }
			}
			effect->controls()->loadSettings(element);
			auto saved = doc.createElement("controls");
			effect->controls()->saveSettings(doc, saved);
			QString xml;
			QTextStream stream(&xml);
			saved.save(stream, 0);
			std::printf("LEGACY %s: %s\n", qPrintable(name), qPrintable(xml.simplified()));
			delete effect;
		}
		else if (kind == "instrument")
		{
			auto track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
			auto instrument = track->loadInstrument(name);
			if (!instrument)
			{
				std::printf("FAIL %s: could not instantiate\n", qPrintable(name));
				++failures;
				continue;
			}
			// Instrument views expect to live inside the track's real instrument window
			QApplication::processEvents();
			gui::InstrumentTrackWindow* window = nullptr;
			for (auto widget : QApplication::allWidgets())
			{
				auto trackView = dynamic_cast<gui::InstrumentTrackView*>(widget);
				if (trackView && trackView->model() == track) { window = trackView->getInstrumentTrackWindow(); }
			}
			if (!window)
			{
				std::printf("FAIL %s: no instrument window\n", qPrintable(name));
				++failures;
				continue;
			}
			applyInstrumentParams(instrument, params);
			const bool audioOk = runInstrument(track, tag);
			if (!audioOk) { ++failures; }
			save(window, outDir.filePath(tag + ".png"));
		}
	}

	std::printf("%d failure(s)\n", failures);
	std::fflush(stdout);
	std::_Exit(failures ? 1 : 0);
}
