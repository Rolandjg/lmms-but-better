/* Copyright (c) 2026 LMMS developers. SPDX-License-Identifier: GPL-2.0-or-later */
#include <QtTest>
#include <memory>

#include "AudioDummy.h"
#include "AudioEngine.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "InstrumentTrack.h"
#include "InstrumentTrackView.h"
#include "InstrumentTrackWindow.h"
#include "MainApplication.h"
#include "MainWindow.h"
#include "Mixer.h"
#include "MixerChannelLcdSpinBox.h"
#include "SampleTrack.h"
#include "SampleTrackView.h"
#include "SampleTrackWindow.h"
#include "Song.h"
#include "SongEditor.h"

using namespace lmms;

class MixerChannelLcdSpinBoxTest : public QObject
{
	Q_OBJECT
	QTemporaryDir m_configDir;
	std::unique_ptr<gui::GuiApplication> m_gui;

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
	}

	void altClickCreatesAndAssignsChannel_data()
	{
		QTest::addColumn<bool>("sampleTrack");
		QTest::addColumn<bool>("trackWindow");
		QTest::newRow("instrument-row") << false << false;
		QTest::newRow("instrument-window") << false << true;
		QTest::newRow("sample-row") << true << false;
		QTest::newRow("sample-window") << true << true;
	}

	void altClickCreatesAndAssignsChannel()
	{
		QFETCH(bool, sampleTrack);
		QFETCH(bool, trackWindow);
		auto guard = Engine::audioEngine()->requestChangesGuard();
		std::unique_ptr<Track> track;
		IntModel* channelModel;
		if (sampleTrack)
		{
			auto sample = std::make_unique<SampleTrack>(Engine::getSong());
			channelModel = sample->mixerChannelModel();
			track = std::move(sample);
		}
		else
		{
			auto instrument = std::make_unique<InstrumentTrack>(Engine::getSong());
			channelModel = instrument->mixerChannelModel();
			track = std::move(instrument);
		}
		track->setName("Alt-click track");
		track->setColor(QColor{Qt::cyan});
		QCoreApplication::processEvents();
		gui::TrackView* view = nullptr;
		for (auto* candidate : m_gui->songEditor()->findChildren<gui::TrackView*>())
		{
			if (candidate->getTrack() == track.get()) { view = candidate; break; }
		}
		// Destroy the view while its track model is still alive.
		auto deleteView = [this](gui::TrackView* trackView)
		{
			if (trackView)
			{
				m_gui->songEditor()->m_editor->removeTrackView(trackView);
				delete trackView;
			}
		};
		std::unique_ptr<gui::TrackView, decltype(deleteView)> viewOwner{view, deleteView};
		QVERIFY(view);
		std::unique_ptr<QWidget> window;
		if (trackWindow)
		{
			if (sampleTrack)
			{
				window = std::make_unique<gui::SampleTrackWindow>(qobject_cast<gui::SampleTrackView*>(view));
			}
			else
			{
				window = std::make_unique<gui::InstrumentTrackWindow>(qobject_cast<gui::InstrumentTrackView*>(view));
			}
		}
		auto* control = trackWindow
			? window->findChild<gui::MixerChannelLcdSpinBox*>()
			: view->findChild<gui::MixerChannelLcdSpinBox*>();
		QVERIFY(control);
		const auto initialChannels = Engine::mixer()->numChannels();
		const QPoint numberPosition{5, 5};
		QTest::mouseClick(control, Qt::LeftButton, Qt::NoModifier, numberPosition);
		QCOMPARE(Engine::mixer()->numChannels(), initialChannels);
		QCOMPARE(channelModel->value(), 0);

		for (int i = 0; i < 2; ++i)
		{
			QTest::mouseClick(control, Qt::LeftButton, Qt::AltModifier, numberPosition);
			const auto newIndex = initialChannels + i;
			QCOMPARE(Engine::mixer()->numChannels(), newIndex + 1);
			QCOMPARE(channelModel->value(), newIndex);
			auto* channel = Engine::mixer()->mixerChannel(newIndex);
			QCOMPARE(channel->m_name, track->name());
			QVERIFY(channel->color() == track->color());
		}

		// Qt sends a separate double-click event after the first click.
		QTest::mouseDClick(control, Qt::LeftButton, Qt::AltModifier, numberPosition);
		QCOMPARE(Engine::mixer()->numChannels(), initialChannels + 2);
		QCOMPARE(channelModel->value(), initialChannels + 1);
	}

	void cleanupTestCase()
	{
		m_gui->mainWindow()->deleteLater();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		m_gui.reset();
		NotePlayHandleManager::free();
	}
};

int main(int argc, char** argv)
{
	gui::MainApplication app{argc, argv};
	MixerChannelLcdSpinBoxTest test;
	return QTest::qExec(&test, argc, argv);
}
#include "MixerChannelLcdSpinBoxTest.moc"
