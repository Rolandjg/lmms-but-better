#include "BounceToAudio.h"

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QPointer>
#include <QTemporaryFile>
#include <QTimer>

#include "Engine.h"
#include "GuiApplication.h"
#include "InstrumentTrack.h"
#include "MainWindow.h"
#include "MidiClip.h"
#include "PathUtil.h"
#include "Mixer.h"
#include "PatternStore.h"
#include "ProjectJournal.h"
#include "ProjectRenderer.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TrackContainerView.h"
#include "TrackView.h"

namespace lmms::gui
{

SampleTrack* bounceToAudio(TrackView* sourceView, MidiClip* clip)
{
	auto song = Engine::getSong();
	auto source = sourceView->getTrack();
	if (song->isExporting() || source->trackContainer() != song) { return nullptr; }

	const auto title = QObject::tr("Bounce to audio");
	// The audio is written next to the project, so an unnamed project needs a location first
	if (song->projectFileName().isEmpty())
	{
		QMessageBox::information(sourceView, title,
			QObject::tr("Bounced audio is stored next to the project file. Please save the project first."));
		getGUI()->mainWindow()->saveProjectAs();
		if (song->projectFileName().isEmpty()) { return nullptr; }
	}

	// Start at the first audible clip so leading silence is not rendered.
	TimePos begin = clip ? clip->startPosition() : TimePos{std::numeric_limits<int>::max()};
	TimePos end = clip ? clip->endPosition() : TimePos{0};
	if (!clip)
	{
		for (auto item : source->getClips())
		{
			if (item->isMuted()) { continue; }
			begin = std::min(begin, item->startPosition());
			end = std::max(end, item->endPosition());
		}
		if (end <= begin) { begin = end; }
	}
	if (end <= begin)
	{
		QMessageBox::information(sourceView, title, QObject::tr("There is no audio to bounce."));
		return nullptr;
	}

	const auto name = clip ? clip->name() : source->name();
	auto basename = name;
	basename.replace(QRegularExpression("[^\\p{L}\\p{N}_-]+"), "_");
	basename = basename.left(80);
	if (basename.isEmpty()) { basename = "bounce"; }
	const auto directory = QFileInfo(song->projectFileName()).absoluteDir();
	// Reserve a unique file in the project directory; never overwrite an earlier bounce.
	QTemporaryFile output(directory.filePath(basename + "-bounce-XXXXXX.wav"));
	if (!output.open())
	{
		QMessageBox::critical(sourceView, title, QObject::tr("Could not create audio in the project directory:\n%1")
			.arg(output.errorString()));
		return nullptr;
	}
	const auto filename = output.fileName();
	output.close();

	const auto position = song->getPlayPos(Song::PlayMode::Song);
	song->stop();
	auto journal = Engine::projectJournal();
	const bool journalling = journal->isJournalling();
	journal->setJournalling(false);

	std::vector<std::pair<Track*, bool>> trackMutes;
	for (auto track : song->tracks())
	{
		// Keep automation running so instrument parameters and tempo are rendered.
		if (track->type() == Track::Type::Automation) { continue; }
		trackMutes.emplace_back(track, track->isMuted());
		track->setMuted(track != source);
	}
	std::vector<std::pair<Clip*, bool>> clipMutes;
	if (clip)
	{
		for (auto item : source->getClips())
		{
			clipMutes.emplace_back(item, item->isMuted());
			item->setMuted(item != clip);
		}
	}

	// Capture the source before it enters the mixer and route the bounce to the same mixer channel, so the
	// bounced track goes through exactly the same channel/master processing as the source did and can
	// replace it without changing the level or the effects.
	auto tapHandles = std::vector<const AudioBusHandle*>{};
	auto mixerChannel = 0;
	auto addTap = [&](Track* track)
	{
		if (auto instrumentTrack = dynamic_cast<InstrumentTrack*>(track))
		{
			tapHandles.push_back(instrumentTrack->audioBusHandle());
			return instrumentTrack->mixerChannelModel()->value();
		}
		if (auto sampleTrack = dynamic_cast<SampleTrack*>(track))
		{
			tapHandles.push_back(sampleTrack->audioBusHandle());
			return sampleTrack->mixerChannelModel()->value();
		}
		return -1;
	};
	if (source->type() == Track::Type::Pattern)
	{
		auto channels = std::vector<int>{};
		for (auto track : Engine::patternStore()->tracks())
		{
			if (const auto channel = addTap(track); channel >= 0) { channels.push_back(channel); }
		}
		const bool shared = !channels.empty()
			&& std::all_of(channels.begin(), channels.end(), [&](int c) { return c == channels.front(); });
		if (shared) { mixerChannel = channels.front(); }
		else
		{
			// The pattern's instruments use different mixer channels, so take the master input instead and
			// route the bounce straight to master
			tapHandles.clear();
		}
	}
	else { mixerChannel = std::max(addTap(source), 0); }

	auto engine = Engine::audioEngine();
	engine->requestChangeInModel();
	Engine::mixer()->setBounceTap(tapHandles);
	engine->doneChangeInModel();
	const OutputSettings settings(engine->outputSampleRate(), 0, OutputSettings::BitDepth::Depth32Bit);
	engine->storeAudioDevice();
	bool ready = false;
	bool cancelled = false;
	{
		ProjectRenderer renderer(settings, ProjectRenderer::ExportFileFormat::Wave, filename, begin, end);
		ready = renderer.isReady();
		if (ready)
		{
			renderer.setRenderTail(true);
			QProgressDialog progress(QObject::tr("Bouncing %1…").arg(name), QObject::tr("Cancel"), 0, 100, sourceView);
			progress.setWindowTitle(title);
			progress.setWindowModality(Qt::ApplicationModal);
			progress.setAutoClose(false);
			progress.setAutoReset(false);
			QObject::connect(&renderer, &ProjectRenderer::progressChanged, &progress, &QProgressDialog::setValue);
			QObject::connect(&renderer, &QThread::finished, &progress, &QDialog::accept);
			QObject::connect(&progress, &QProgressDialog::canceled, &progress, [&]
			{
				cancelled = true;
				renderer.abortProcessing();
				progress.reject();
			});
			renderer.startProcessing();
			progress.exec();
			renderer.wait();
		}
	}
	// Restoring the device closes the WAV and finalizes its header before loading it.
	engine->restoreAudioDevice();
	engine->requestChangeInModel();
	Engine::mixer()->clearBounceTap();
	engine->doneChangeInModel();
	for (auto [item, muted] : clipMutes) { item->setMuted(muted); }
	for (auto [track, muted] : trackMutes) { track->setMuted(muted); }
	song->setPlayPos(position.getTicks(), Song::PlayMode::Song);
	journal->setJournalling(journalling);
	if (cancelled) { return nullptr; }
	if (!ready || !QFileInfo::exists(filename) || SampleBuffer::fromFile(filename)->size() == 0)
	{
		QMessageBox::critical(sourceView, title, QObject::tr("Could not render the audio file."));
		return nullptr;
	}

	output.setAutoRemove(false);
	auto sampleTrack = new SampleTrack(song);
	sampleTrack->setName(QObject::tr("%1 (bounced)").arg(name));
	auto sampleClip = new SampleClip(sampleTrack);
	sampleClip->setSampleFile(filename);
	sampleClip->setName(name);
	sampleClip->movePosition(begin);
	sampleTrack->mixerChannelModel()->setValue(mixerChannel);
	// The bounce replaces what it was rendered from; unmuting the source layers both again
	if (clip) { clip->setMuted(true); }
	else { source->setMuted(true); }
	auto containerView = sourceView->trackContainerView();
	auto sampleView = containerView->createTrackView(sampleTrack);
	containerView->moveTrackView(sampleView, containerView->trackViews().indexOf(sourceView) + 1);
	song->setModified();
	return sampleTrack;
}




void freezeTrack(TrackView* sourceView)
{
	const auto source = sourceView->getTrack();
	if (auto frozen = bounceToAudio(sourceView))
	{
		frozen->setName(QObject::tr("%1 (frozen)").arg(source->name()));
		frozen->setFrozenFrom(source->name());
	}
}




void unfreezeTrack(TrackView* frozenView)
{
	auto frozen = dynamic_cast<SampleTrack*>(frozenView->getTrack());
	if (!frozen || frozen->frozenFrom().isEmpty()) { return; }
	auto song = Engine::getSong();

	// prefer a muted track of that name: that's the one the freeze muted
	Track* source = nullptr;
	for (auto track : song->tracks())
	{
		if (track == frozen || track->name() != frozen->frozenFrom()) { continue; }
		if (!source || (track->isMuted() && !source->isMuted())) { source = track; }
	}
	const auto title = QObject::tr("Unfreeze");
	const auto question = source
		? QObject::tr("Remove \"%1\" and its rendered audio, and play \"%2\" again?").arg(frozen->name(), source->name())
		: QObject::tr("The track \"%1\" this was frozen from no longer exists. Remove \"%2\" and its rendered audio "
			"anyway?").arg(frozen->frozenFrom(), frozen->name());
	if (QMessageBox::question(frozenView, title, question) != QMessageBox::Yes) { return; }

	if (source) { source->setMuted(false); }
	// The rendered files are generated next to the project; nothing else should be using them
	QStringList files;
	for (auto clip : frozen->getClips())
	{
		if (auto sampleClip = dynamic_cast<SampleClip*>(clip)) { files << PathUtil::toAbsolute(sampleClip->sampleFile()); }
	}
	song->setModified();
	// Called from the frozen track's own menu: delete it once that has returned
	auto containerView = frozenView->trackContainerView();
	QTimer::singleShot(0, containerView, [containerView, view = QPointer<TrackView>(frozenView), files] {
		if (!view) { return; }
		containerView->deleteTrackView(view);
		for (const auto& file : files)
		{
			if (QFileInfo(file).fileName().contains("-bounce-")) { QFile::remove(file); }
		}
	});
}

} // namespace lmms::gui
