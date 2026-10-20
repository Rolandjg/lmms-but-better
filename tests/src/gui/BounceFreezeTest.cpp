/* Copyright (c) 2026 LMMS developers. SPDX-License-Identifier: GPL-2.0-or-later */
#include <QtTest>
#include <memory>
#include <QTreeWidget>
#include <QMessageBox>
#include <QAbstractButton>
#include <QPointer>
#include <QTimer>

#include "AudioDummy.h"
#include "BounceToAudio.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "MainApplication.h"
#include "MainWindow.h"
#include "Mixer.h"
#include "ProjectHealthDialog.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"
#include "SongEditor.h"
#include "TrackView.h"

using namespace lmms;

class BounceFreezeTest : public QObject
{
	Q_OBJECT
	QTemporaryDir m_configDir;
	std::unique_ptr<gui::GuiApplication> m_gui;

	gui::TrackView* viewOf(Track* track)
	{
		for (auto view : m_gui->songEditor()->m_editor->trackViews())
		{
			if (view->getTrack() == track) { return view; }
		}
		return nullptr;
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
	}

	void freezeReplacesSourceTransparently()
	{
		auto song = Engine::getSong();
		song->createEmptyProject();
		QVERIFY(song->guiSaveProjectAs(m_configDir.filePath("bounce.mmpz")));

		auto mixer = Engine::mixer();
		const int channel = mixer->createChannel();
		mixer->createChannelSend(channel, 0);
		auto source = new SampleTrack(song);
		source->setName("Pad");
		source->mixerChannelModel()->setValue(channel);
		const auto rate = Engine::audioEngine()->outputSampleRate();
		auto clip = new SampleClip(source);
		clip->setSampleBuffer(std::make_shared<SampleBuffer>(std::vector<SampleFrame>(rate * 2, SampleFrame{0.4f}), rate));
		clip->changeLength(TimePos{96});
		song->setMasterVolume(50);
		QCoreApplication::sendPostedEvents();
		auto view = viewOf(source);
		QVERIFY(view);

		gui::freezeTrack(view);
		song->setMasterVolume(100);

		SampleTrack* frozen = nullptr;
		for (auto track : song->tracks())
		{
			if (auto sampleTrack = dynamic_cast<SampleTrack*>(track); sampleTrack && sampleTrack != source)
			{
				frozen = sampleTrack;
			}
		}
		QVERIFY(frozen);
		QCOMPARE(frozen->frozenFrom(), QString("Pad"));
		QVERIFY(frozen->name().contains("frozen"));
		// same mixer channel, so channel and master processing apply exactly once, like for the source
		QCOMPARE(frozen->mixerChannelModel()->value(), channel);
		QVERIFY(source->isMuted());
		QVERIFY(!frozen->isMuted());

		// rendered before the master section: master volume is not baked in
		QCOMPARE(frozen->getClips().size(), std::size_t{1});
		auto rendered = dynamic_cast<SampleClip*>(frozen->getClips().front());
		QVERIFY(rendered);
		float peak = 0.f;
		const auto buffer = rendered->sample().buffer();
		for (const auto& frame : *buffer) { peak = std::max(peak, std::abs(frame.left())); }
		QVERIFY2(std::abs(peak - 0.4f) < 0.01f, qPrintable(QString("peak %1").arg(peak)));
		QVERIFY(rendered->sampleFile().contains("-bounce-"));
	}

	void healthListsBouncedAudio()
	{
		gui::ProjectHealthDialog dialog;
		auto files = dialog.findChild<QTreeWidget*>();
		QVERIFY(files);
		QVERIFY(files->topLevelItemCount() >= 1);
		QCOMPARE(files->topLevelItem(0)->text(1), QString("Bounced audio"));
		QCOMPARE(files->topLevelItem(0)->text(2), QString("Project folder"));
		if (const auto snapshot = qEnvironmentVariable("LMMS_HEALTH_SNAPSHOT"); !snapshot.isEmpty())
		{
			dialog.show();
			QCoreApplication::processEvents();
			dialog.grab().save(snapshot);
		}
	}

	void unfreezePreservesAudioUsedElsewhere_data()
	{
		QTest::addColumn<bool>("shared");
		QTest::newRow("unused-render-is-removed") << false;
		QTest::newRow("shared-render-is-kept") << true;
	}

	void unfreezeFindsRenamedSourceAfterReload()
	{
		auto song = Engine::getSong();
		SampleTrack* frozen = nullptr;
		for (auto track : song->tracks())
		{
			auto sample = dynamic_cast<SampleTrack*>(track);
			if (sample && !sample->frozenFrom().isEmpty()) { frozen = sample; break; }
		}
		QVERIFY(frozen);
		const auto id = frozen->frozenFromId();
		QVERIFY(!id.isEmpty());
		QVERIFY(song->guiSaveProjectAs(m_configDir.filePath("freeze-reload.mmp")));
		song->loadProject(m_configDir.filePath("freeze-reload.mmp"));
		Track* source = nullptr;
		frozen = nullptr;
		for (auto track : song->tracks())
		{
			if (track->freezeId() == id) { source = track; }
			auto sample = dynamic_cast<SampleTrack*>(track);
			if (sample && sample->frozenFromId() == id) { frozen = sample; }
		}
		QVERIFY(source);
		QVERIFY(frozen);
		auto duplicate = source->clone();
		QVERIFY(duplicate);
		QVERIFY(duplicate->freezeId().isEmpty());
		duplicate->setMuted(true);
		source->setName("Renamed source");
		QCoreApplication::sendPostedEvents();
		auto view = viewOf(frozen);
		QVERIFY(view);
		QPointer<SampleTrack> remaining = frozen;
		QTimer confirmer;
		connect(&confirmer, &QTimer::timeout, [] {
			if (auto dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
			{
				if (auto yes = dialog->button(QMessageBox::Yes)) { yes->click(); }
			}
		});
		confirmer.start(10);
		gui::unfreezeTrack(view);
		confirmer.stop();
		QTRY_VERIFY(remaining.isNull());
		QVERIFY(!source->isMuted());
		QVERIFY(duplicate->isMuted());
	}

	void unfreezePreservesAudioUsedElsewhere()
	{
		QFETCH(bool, shared);
		auto song = Engine::getSong();
		song->createEmptyProject();
		QTemporaryDir dir;
		const auto path = dir.filePath("test-bounce-qa.ogg");
		QVERIFY(QFile::copy(qEnvironmentVariable("LMMS_DATA_DIR") + "/samples/drums/kick01.ogg", path));
		auto source = new SampleTrack(song);
		source->setName("Original");
		source->setMuted(true);
		QPointer<SampleTrack> frozen = new SampleTrack(song);
		frozen->setFrozenFrom(source->name());
		(new SampleClip(frozen))->setSampleFile(path);
		if (shared)
		{
			auto other = new SampleTrack(song);
			(new SampleClip(other))->setSampleFile(path);
		}
		QCoreApplication::sendPostedEvents();
		auto view = viewOf(frozen);
		QVERIFY(view);
		QTimer confirmer;
		connect(&confirmer, &QTimer::timeout, [] {
			if (auto dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
			{
				if (auto yes = dialog->button(QMessageBox::Yes)) { yes->click(); }
			}
		});
		confirmer.start(10);
		gui::unfreezeTrack(view);
		confirmer.stop();
		QTRY_VERIFY(frozen.isNull());
		QVERIFY(!source->isMuted());
		QCOMPARE(QFileInfo::exists(path), shared);
	}

	void cleanupTestCase()
	{
		Engine::getSong()->guiSaveProject();
		m_gui->mainWindow()->deleteLater();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		m_gui.reset();
	}
};

int main(int argc, char** argv)
{
	gui::MainApplication app{argc, argv};
	BounceFreezeTest test;
	return QTest::qExec(&test, argc, argv);
}
#include "BounceFreezeTest.moc"
