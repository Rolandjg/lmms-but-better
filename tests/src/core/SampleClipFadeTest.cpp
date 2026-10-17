#include <QDomDocument>
#include <QtTest>

#include "Engine.h"
#include "ProjectRenderer.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"

class SampleClipFadeTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase() { lmms::Engine::init(true); }
	void cleanupTestCase() { lmms::Engine::destroy(); }

	void fadeCurve()
	{
		using lmms::SampleClip;
		QCOMPARE(SampleClip::fadeGain(50.f, 100.f, 0.f, 0.f), 1.f);
		QCOMPARE(SampleClip::fadeGain(0.f, 100.f, 10.f, 0.f), 0.f);
		QCOMPARE(SampleClip::fadeGain(10.f, 100.f, 10.f, 0.f), 1.f);
		QCOMPARE(SampleClip::fadeGain(100.f, 100.f, 0.f, 10.f), 0.f);
		// Equal power: halfway through a fade is -3 dB
		QVERIFY(std::abs(SampleClip::fadeGain(5.f, 100.f, 10.f, 0.f) - std::sqrt(0.5f)) < 1e-4f);
		// Fades longer than the clip are squeezed so they meet in the middle
		QVERIFY(std::abs(SampleClip::fadeGain(50.f, 100.f, 100.f, 100.f) - 1.f) < 1e-4f);
		QVERIFY(SampleClip::fadeGain(25.f, 100.f, 100.f, 100.f) < 0.75f);
	}

	void settingsRoundTrip()
	{
		using namespace lmms;
		SampleTrack track(Engine::getSong());
		auto clip = new SampleClip(&track);
		clip->changeLength(TimePos{192});
		clip->setFadeIn(TimePos{24});
		clip->setFadeOut(TimePos{1000}); // clamped to the clip length
		clip->setGain(0.5f);
		QCOMPARE(static_cast<int>(clip->fadeOut()), 192);

		QDomDocument doc;
		auto element = doc.createElement("sampleclip");
		clip->saveSettings(doc, element);
		auto copy = new SampleClip(&track);
		copy->loadSettings(element);
		QCOMPARE(static_cast<int>(copy->fadeIn()), 24);
		QCOMPARE(static_cast<int>(copy->fadeOut()), 192);
		QCOMPARE(copy->gain(), 0.5f);
	}

	void normalizeHitsFullScale()
	{
		using namespace lmms;
		SampleTrack track(Engine::getSong());
		auto clip = new SampleClip(&track);
		const auto rate = Engine::audioEngine()->outputSampleRate();
		std::vector<SampleFrame> frames(rate, SampleFrame{0.1f});
		frames[rate / 2] = SampleFrame{0.25f, -0.4f};
		clip->setSampleBuffer(std::make_shared<SampleBuffer>(std::move(frames), rate));
		clip->normalize();
		QVERIFY(std::abs(clip->gain() - 2.5f) < 1e-4f);
	}

	void rendersFadedAudio()
	{
		using namespace lmms;
		auto song = Engine::getSong();
		auto engine = Engine::audioEngine();
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const auto path = directory.filePath("fade.wav");
		SampleTrack track(song);
		auto clip = new SampleClip(&track);
		const auto rate = engine->outputSampleRate();
		clip->setSampleBuffer(std::make_shared<SampleBuffer>(
			std::vector<SampleFrame>(rate * 4, SampleFrame{0.5f}), rate));
		clip->changeLength(TimePos{192});
		clip->setFadeIn(TimePos{96});
		clip->setGain(0.5f);
		engine->storeAudioDevice();
		bool finished;
		{
			ProjectRenderer renderer(OutputSettings(rate, 0, OutputSettings::BitDepth::Depth32Bit),
				ProjectRenderer::ExportFileFormat::Wave, path, TimePos{0}, TimePos{192});
			renderer.startProcessing();
			finished = renderer.wait(10000);
			if (!finished) { renderer.abortProcessing(); }
		}
		engine->restoreAudioDevice();
		QVERIFY(finished);

		const auto buffer = SampleBuffer::fromFile(path);
		const auto framesPerTick = Engine::framesPerTick();
		const auto at = [&](double ticks) { return std::abs(buffer->data()[static_cast<std::size_t>(ticks * framesPerTick)].left()); };
		QVERIFY(buffer->size() > 180 * framesPerTick);
		// Silent at the start, ramping up through the fade, then the full (gain-scaled) level.
		// The track's default volume and panning law leave some headroom, so compare ratios.
		const float full = at(150);
		QVERIFY(full > 0.05f);
		QVERIFY(at(1) < full * 0.1f);
		QVERIFY(std::abs(at(48) / full - std::sqrt(0.5f)) < 0.05f);
		QVERIFY(std::abs(at(120) / full - 1.f) < 0.01f);
	}
};

QTEST_GUILESS_MAIN(SampleClipFadeTest)
#include "SampleClipFadeTest.moc"
