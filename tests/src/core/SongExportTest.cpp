#include <QtTest>

#include "Engine.h"
#include "Song.h"
#include "ProjectRenderer.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleTrack.h"

class SongExportTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase() { lmms::Engine::init(true); }
	void cleanupTestCase() { lmms::Engine::destroy(); }

	void rendersAudioAtClipPosition()
	{
		using namespace lmms;
		auto song = Engine::getSong();
		auto engine = Engine::audioEngine();
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const auto path = directory.filePath("bounce.wav");
		SampleTrack track(song);
		auto clip = new SampleClip(&track);
		const auto rate = engine->outputSampleRate();
		clip->setSampleBuffer(std::make_shared<SampleBuffer>(
			std::vector<SampleFrame>(rate * 2, SampleFrame{0.25f}), rate));
		clip->movePosition(TimePos{192});
		clip->changeLength(TimePos{96});
		engine->storeAudioDevice();
		bool ready;
		bool finished;
		{
			ProjectRenderer renderer(OutputSettings(rate, 0, OutputSettings::BitDepth::Depth32Bit),
				ProjectRenderer::ExportFileFormat::Wave, path, TimePos{192}, TimePos{288});
			ready = renderer.isReady();
			renderer.startProcessing();
			finished = renderer.wait(10000);
			if (!finished) { renderer.abortProcessing(); }
		}
		engine->restoreAudioDevice();
		QVERIFY(ready);
		QVERIFY(finished);
		const auto buffer = SampleBuffer::fromFile(path);
		const auto expectedFrames = 96 * Engine::framesPerTick();
		QVERIFY(std::abs(static_cast<double>(buffer->size()) - expectedFrames) < 2 * engine->framesPerPeriod());
		QVERIFY(std::any_of(buffer->begin(), buffer->end(), [](const auto& frame)
		{
			return std::abs(frame.left()) > 0.01f;
		}));
	}

	void explicitRangeIgnoresExportLoopSettings()
	{
		using namespace lmms;
		auto song = Engine::getSong();
		auto& timeline = song->getTimeline(Song::PlayMode::Song);
		timeline.setLoopPoints(TimePos{48}, TimePos{96});
		song->setRenderBetweenMarkers(true);
		song->setExportLoop(true);
		song->setLoopRenderCount(4);

		song->startExport(TimePos{192}, TimePos{384});
		QCOMPARE(song->getPlayPos().getTicks(), 192);
		QCOMPARE(song->getExportProgress(), 0);
		QVERIFY(!song->isExportDone());
		song->setPlayPos(288);
		QCOMPARE(song->getExportProgress(), 50);
		song->setPlayPos(384);
		QVERIFY(song->isExportDone());
		QCOMPARE(song->getExportProgress(), 100);
		song->stopExport();

		// Bouncing must leave the user's normal export settings untouched.
		QCOMPARE(song->getLoopRenderCount(), 4);
		QCOMPARE(timeline.loopBegin().getTicks(), 48);
		QCOMPARE(timeline.loopEnd().getTicks(), 96);
		song->startExport();
		QCOMPARE(song->getPlayPos().getTicks(), 48);
		song->setPlayPos(96);
		QVERIFY(song->isExportDone());
		song->stopExport();
		song->setRenderBetweenMarkers(false);
		song->setExportLoop(false);
		song->setLoopRenderCount(1);
	}
};

QTEST_GUILESS_MAIN(SongExportTest)
#include "SongExportTest.moc"
