#include <QtTest>

#include <QTemporaryDir>

#include "AudioBusHandle.h"
#include "Effect.h"
#include "EffectChain.h"
#include "EffectControls.h"
#include "Engine.h"
#include "Mixer.h"
#include "ProjectRenderer.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"

namespace
{

using namespace lmms;

//! Delays its input by a fixed number of frames and reports that as its latency, like a lookahead limiter
class DelayEffect : public Effect
{
	class Controls : public EffectControls
	{
	public:
		using EffectControls::EffectControls;
		int controlCount() override { return 0; }
		gui::EffectControlDialog* createView() override { return nullptr; }
		void saveSettings(QDomDocument&, QDomElement&) override {}
		void loadSettings(const QDomElement&) override {}
		QString nodeName() const override { return "delaycontrols"; }
	};

public:
	DelayEffect(f_cnt_t frames) : Effect(nullptr, nullptr, nullptr), m_controls(this), m_ring(frames) {}

	EffectControls* controls() override { return &m_controls; }
	f_cnt_t latency() const override { return static_cast<f_cnt_t>(m_ring.size()); }

protected:
	ProcessStatus processImpl(SampleFrame* buf, const f_cnt_t frames) override
	{
		for (f_cnt_t f = 0; f < frames; ++f)
		{
			std::swap(buf[f], m_ring[m_pos]);
			m_pos = (m_pos + 1) % m_ring.size();
		}
		return ProcessStatus::Continue;
	}

private:
	Controls m_controls;
	std::vector<SampleFrame> m_ring;
	std::size_t m_pos = 0;
};

constexpr f_cnt_t ClickFrame = 100;

SampleTrack* addClickTrack(float level = 0.5f)
{
	const auto rate = Engine::audioEngine()->outputSampleRate();
	auto track = new SampleTrack(Engine::getSong());
	auto frames = std::vector<SampleFrame>(rate, SampleFrame{});
	frames[ClickFrame] = SampleFrame{level};
	auto clip = new SampleClip(track);
	clip->setSampleBuffer(std::make_shared<SampleBuffer>(std::move(frames), rate));
	clip->movePosition(TimePos{0});
	clip->changeLength(TimePos{96});
	return track;
}

//! Frames (and their level) above @p threshold
std::vector<std::pair<f_cnt_t, float>> loudFrames(const QString& path, float threshold)
{
	std::vector<std::pair<f_cnt_t, float>> result;
	const auto buffer = SampleBuffer::fromFile(path);
	f_cnt_t f = 0;
	for (const auto& frame : *buffer)
	{
		if (std::abs(frame.left()) > threshold) { result.emplace_back(f, frame.left()); }
		++f;
	}
	return result;
}

bool render(const QString& path)
{
	auto engine = Engine::audioEngine();
	engine->storeAudioDevice();
	bool finished = false;
	{
		ProjectRenderer renderer(OutputSettings(engine->outputSampleRate(), 0, OutputSettings::BitDepth::Depth32Bit),
			ProjectRenderer::ExportFileFormat::Wave, path, TimePos{0}, TimePos{96});
		renderer.startProcessing();
		finished = renderer.wait(20000);
	}
	engine->restoreAudioDevice();
	return finished;
}

} // namespace


class LatencyCompensationTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase() { Engine::init(true); }
	void cleanupTestCase() { Engine::destroy(); }
	void init() { Engine::getSong()->createEmptyProject(); }

	void baseline()
	{
		// Where a click lands without any latency (sample playback may add a frame of its own)
		addClickTrack();
		QTemporaryDir dir;
		const auto path = dir.filePath("baseline.wav");
		QVERIFY(render(path));
		const auto loud = loudFrames(path, 0.25f);
		QCOMPARE(loud.size(), std::size_t{1});
		m_clickFrame = loud.front().first;
		m_clickLevel = loud.front().second;
		QVERIFY(m_clickFrame >= ClickFrame && m_clickFrame <= ClickFrame + 2);
		QCOMPARE(Engine::mixer()->outputLatency(), f_cnt_t{0});
	}

	void parallelPathsStayAligned()
	{
		constexpr f_cnt_t Latency = 777;
		auto delayed = addClickTrack();
		addClickTrack();
		delayed->audioBusHandle()->effects()->appendEffect(new DelayEffect(Latency));

		QTemporaryDir dir;
		const auto path = dir.filePath("aligned.wav");
		QVERIFY(render(path));

		// Both clicks land on the same frame (summing up), where the clip put them: the latency is
		// compensated between the tracks and trimmed from the start of the file
		const auto loud = loudFrames(path, 0.25f);
		QCOMPARE(loud.size(), std::size_t{1});
		QCOMPARE(loud.front().first, m_clickFrame);
		QVERIFY2(std::abs(loud.front().second - 2 * m_clickLevel) < 0.01f,
			qPrintable(QString("clicks not summed: %1 vs %2").arg(loud.front().second).arg(m_clickLevel)));
		QCOMPARE(Engine::mixer()->outputLatency(), Latency);
	}

	void sendsAreCompensated()
	{
		// A track with a latent effect on its own mixer channel and a dry track on master
		constexpr f_cnt_t Latency = 300;
		auto mixer = Engine::mixer();
		const int channel = mixer->createChannel();
		mixer->createChannelSend(channel, 0);
		mixer->mixerChannel(channel)->m_fxChain.appendEffect(new DelayEffect(Latency));
		auto routed = addClickTrack();
		routed->mixerChannelModel()->setValue(channel);
		addClickTrack();

		QTemporaryDir dir;
		const auto path = dir.filePath("sends.wav");
		QVERIFY(render(path));
		const auto loud = loudFrames(path, 0.25f);
		QCOMPARE(loud.size(), std::size_t{1});
		QCOMPARE(loud.front().first, m_clickFrame);
	}

private:
	f_cnt_t m_clickFrame = ClickFrame;
	float m_clickLevel = 0.5f;
};

QTEST_GUILESS_MAIN(LatencyCompensationTest)
#include "LatencyCompensationTest.moc"
