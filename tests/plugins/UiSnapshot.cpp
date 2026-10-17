/*
 * UiSnapshot.cpp - developer tool: renders the main application windows to PNG files,
 *                  for reviewing theme changes without a display.
 *
 * Usage: UiSnapshot <outdir> [project file]
 * Run with QT_QPA_PLATFORM=offscreen, LMMS_PLUGIN_DIR and LMMS_DATA_DIR set.
 */

#include <QApplication>
#include <QDir>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QWidget>
#include <cstdio>

#include "AudioDummy.h"
#include "AudioEngine.h"
#include "AutomationEditor.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "InstrumentTrack.h"
#include "InstrumentTrackView.h"
#include "InstrumentTrackWindow.h"
#include "MainApplication.h"
#include "MainWindow.h"
#include "MidiClip.h"
#include "Mixer.h"
#include "MixerView.h"
#include "NotePlayHandle.h"
#include "PatternEditor.h"
#include "PatternStore.h"
#include "PianoRoll.h"
#include "SampleClip.h"
#include "SampleClipView.h"
#include "SampleTrack.h"
#include "SetupDialog.h"
#include "SideBar.h"
#include "Song.h"
#include "SongEditor.h"

using namespace lmms;

namespace
{

void settle()
{
	for (int i = 0; i < 5; ++i) { QCoreApplication::processEvents(); }
}

void save(QWidget* widget, const QString& dir, const QString& name)
{
	settle();
	widget->grab().save(dir + "/" + name + ".png");
	std::printf("wrote %s/%s.png\n", qPrintable(dir), qPrintable(name));
}

QMdiSubWindow* subWindowOf(QWidget* w)
{
	for (QWidget* p = w; p; p = p->parentWidget())
	{
		if (auto sub = qobject_cast<QMdiSubWindow*>(p)) { return sub; }
	}
	return nullptr;
}

} // namespace

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		std::fprintf(stderr, "usage: %s <outdir> [project]\n", argv[0]);
		return 1;
	}
	new gui::MainApplication(argc, argv);
	const QString outDir = QString::fromLocal8Bit(argv[1]);
	QDir().mkpath(outDir);

	QTemporaryDir configDir;
	NotePlayHandleManager::init();
	ConfigManager::inst()->loadConfigFile(configDir.path() + "/lmmsrc.xml");
	ConfigManager::inst()->setWorkingDir(configDir.path() + "/work/");
	ConfigManager::inst()->createWorkingDir();
	ConfigManager::inst()->setValue("app", "configured", "1");
	ConfigManager::inst()->setValue("audioengine", "audiodev", AudioDummy::name());
	ConfigManager::inst()->setValue("ui", "enableautosave", "0");

	auto gui = new gui::GuiApplication();
	auto mw = gui->mainWindow();
	mw->resize(1600, 1000);
	mw->show();
	settle();

	// Project loading may pop up modal warnings (e.g. about plugins missing from this build)
	auto dismisser = new QTimer();
	QObject::connect(dismisser, &QTimer::timeout, [] {
		if (auto modal = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
		{
			std::printf("dismissing dialog: %s\n", qPrintable(modal->windowTitle()));
			modal->close();
		}
	});
	dismisser->start(200);

	if (argc > 2) { Engine::getSong()->loadProject(QString::fromLocal8Bit(argv[2])); }
	settle();
	gui->mixerView()->refreshDisplay();
	while (Engine::mixer()->numChannels() < 6) { gui->mixerView()->addNewChannel(); }
	gui->mixerView()->setCurrentMixerChannel(1);
	settle();
	std::printf("project has %d tracks\n", static_cast<int>(Engine::getSong()->tracks().size()));

	// Arrangement markers, so the timeline's marker lane has something to show
	{
		auto& timeline = Engine::getSong()->getTimeline(Song::PlayMode::Song);
		if (timeline.markers().empty())
		{
			const auto bar = TimePos::ticksPerBar();
			timeline.addMarker(TimePos(0), "Intro", QColor(0x5b, 0x8d, 0xef));
			timeline.addMarker(TimePos(2 * bar), "Verse", QColor(0x3f, 0xbf, 0x8f));
			timeline.addMarker(TimePos(4 * bar), "Chorus", QColor(0xe0, 0x54, 0x4e));
			timeline.addMarker(TimePos(6 * bar), "Drop", QColor(0xff, 0x7a, 0x3d));
		}
	}

	// Main window with song editor + mixer, the way people usually see it
	auto mdi = mw->findChild<QMdiArea*>();
	for (auto sub : mdi->subWindowList()) { sub->hide(); }
	if (auto sub = subWindowOf(gui->songEditor()))
	{
		sub->show();
		sub->setGeometry(0, 0, 1100, 560);
	}
	if (auto sub = subWindowOf(gui->mixerView()))
	{
		sub->show();
		sub->setGeometry(0, 560, 1300, 340);
	}
	if (auto sub = subWindowOf(gui->patternEditor()))
	{
		sub->show();
		sub->setGeometry(1100, 0, 450, 300);
	}
	save(mw, outDir, "main");

	// Same layout with the instrument browser and a file browser open in the sidebar
	if (auto sideBar = mw->findChild<gui::SideBar*>())
	{
		const auto buttons = sideBar->findChildren<QToolButton*>();
		if (buttons.size() > 1)
		{
			buttons[0]->click();
			save(mw, outDir, "main_plugins");
			buttons[1]->click();
			save(mw, outDir, "main_files");
			buttons[1]->click();
		}
	}

	// A sample track with fades and clip gain, hovered so the handles show
	{
		auto track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
		track->setHeight(72);
		const QString dataDir = qEnvironmentVariable("LMMS_DATA_DIR", "data");
		const auto loops = QStringList{"bassloops/techno_synth01 - 140 BPM.ogg", "bassloops/briff01 - 140 BPM.ogg"};
		for (int i = 0; i < loops.size(); ++i)
		{
			auto sampleClip = new SampleClip(track);
			sampleClip->setSampleFile(QDir(dataDir).absoluteFilePath("samples/" + loops[i]));
			sampleClip->movePosition(TimePos(i * 3 * TimePos::ticksPerBar()));
			sampleClip->changeLength(TimePos(3 * TimePos::ticksPerBar()));
			sampleClip->setFadeIn(TimePos(i == 0 ? TimePos::ticksPerBar() : 0));
			sampleClip->setFadeOut(TimePos(TimePos::ticksPerBar() * (i == 0 ? 1 : 2) / 2));
			sampleClip->setGain(i == 0 ? 1.f : 1.6f);
		}
		settle();
		auto views = mw->findChildren<gui::SampleClipView*>();
		for (auto view : views)
		{
			QEvent enter(QEvent::Enter);
			QApplication::sendEvent(view, &enter);
			view->update();
		}
		if (auto sub = subWindowOf(gui->songEditor())) { sub->raise(); }
		save(gui->songEditor(), outDir, "song_samples");
	}

	// Individual editors
	if (auto sub = subWindowOf(gui->mixerView())) { sub->resize(1100, 520); }
	save(gui->mixerView(), outDir, "mixer");

	InstrumentTrack* instrumentTrack = nullptr;
	MidiClip* clip = nullptr;
	auto allTracks = Engine::getSong()->tracks();
	for (auto track : Engine::patternStore()->tracks()) { allTracks.push_back(track); }
	for (auto track : allTracks)
	{
		auto it = dynamic_cast<InstrumentTrack*>(track);
		if (!it) { continue; }
		if (!instrumentTrack) { instrumentTrack = it; }
		for (auto c : it->getClips())
		{
			auto mc = dynamic_cast<MidiClip*>(c);
			if (mc && mc->notes().size() > 8)
			{
				instrumentTrack = it;
				clip = mc;
				break;
			}
		}
		if (clip) { break; }
	}

	if (clip)
	{
		gui->pianoRoll()->setCurrentMidiClip(clip);
		gui->pianoRoll()->resize(1200, 700);
		gui->pianoRoll()->show();
		if (auto sub = subWindowOf(gui->pianoRoll())) { sub->resize(1200, 700); }
		save(gui->pianoRoll(), outDir, "pianoroll");
	}

	if (instrumentTrack)
	{
		for (auto tv : mw->findChildren<gui::InstrumentTrackView*>())
		{
			if (tv->model() != instrumentTrack) { continue; }
			auto window = tv->getInstrumentTrackWindow();
			window->show();
			if (auto sub = subWindowOf(window)) { sub->show(); }
			save(window, outDir, "instrument");
			break;
		}
	}

	auto setup = new gui::SetupDialog();
	setup->show();
	save(setup, outDir, "settings");
	setup->hide();

	std::fflush(stdout);
	std::_Exit(0);
}
