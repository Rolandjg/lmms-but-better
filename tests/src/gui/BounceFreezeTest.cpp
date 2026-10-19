/* Copyright (c) 2026 LMMS developers. SPDX-License-Identifier: GPL-2.0-or-later */
#include <QtTest>
#include <memory>
#include <QTreeWidget>

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
