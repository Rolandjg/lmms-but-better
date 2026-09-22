#include "BounceToAudio.h"

#include <algorithm>
#include <utility>
#include <vector>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QTemporaryFile>

#include "Engine.h"
#include "MidiClip.h"
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

void bounceToAudio(TrackView* sourceView, MidiClip* clip)
{
	auto song = Engine::getSong();
	auto source = sourceView->getTrack();
	if (song->isExporting() || source->trackContainer() != song) { return; }

	const auto title = QObject::tr("Bounce to audio");
	if (song->projectFileName().isEmpty() && !song->guiSaveProject()) { return; }
	if (song->projectFileName().isEmpty()) { return; }

	const TimePos begin = clip ? clip->startPosition() : TimePos{0};
	TimePos end = begin;
	if (clip) { end = clip->endPosition(); }
	else
	{
		for (auto item : source->getClips())
		{
			if (!item->isMuted()) { end = std::max(end, item->endPosition()); }
		}
	}
	if (end <= begin)
	{
		QMessageBox::information(sourceView, title, QObject::tr("There is no audio to bounce."));
		return;
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
		return;
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

	auto engine = Engine::audioEngine();
	const OutputSettings settings(engine->outputSampleRate(), 0, OutputSettings::BitDepth::Depth32Bit);
	engine->storeAudioDevice();
	bool ready = false;
	bool cancelled = false;
	{
		ProjectRenderer renderer(settings, ProjectRenderer::ExportFileFormat::Wave, filename, begin, end);
		ready = renderer.isReady();
		if (ready)
		{
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
	for (auto [item, muted] : clipMutes) { item->setMuted(muted); }
	for (auto [track, muted] : trackMutes) { track->setMuted(muted); }
	song->setPlayPos(position.getTicks(), Song::PlayMode::Song);
	journal->setJournalling(journalling);
	if (cancelled) { return; }
	if (!ready || !QFileInfo::exists(filename) || SampleBuffer::fromFile(filename)->size() == 0)
	{
		QMessageBox::critical(sourceView, title, QObject::tr("Could not render the audio file."));
		return;
	}

	output.setAutoRemove(false);
	auto sampleTrack = new SampleTrack(song);
	sampleTrack->setName(QObject::tr("%1 (bounced)").arg(name));
	auto sampleClip = new SampleClip(sampleTrack);
	sampleClip->setSampleFile(filename);
	sampleClip->setName(name);
	sampleClip->movePosition(begin);
	// The renderer writes whole audio buffers; trim the last partial buffer in the editor.
	sampleClip->changeLength(end - begin);
	auto containerView = sourceView->trackContainerView();
	auto sampleView = containerView->createTrackView(sampleTrack);
	containerView->moveTrackView(sampleView, containerView->trackViews().indexOf(sourceView) + 1);
	song->setModified();
}

} // namespace lmms::gui
