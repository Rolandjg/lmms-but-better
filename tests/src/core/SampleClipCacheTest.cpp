#include <QDateTime>
#include <QDomDocument>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>
#include <numbers>

#include "Engine.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"

namespace
{

using namespace lmms;

constexpr int FileRate = 44100;

//! Write a 16-bit stereo sine as a WAV file
void writeWav(const QString& path, int frames, float frequency)
{
	QByteArray data;
	const auto put16 = [&data](qint16 v) { data.append(reinterpret_cast<const char*>(&v), 2); };
	const auto put32 = [&data](qint32 v) { data.append(reinterpret_cast<const char*>(&v), 4); };
	data.append("RIFF");
	put32(36 + frames * 4);
	data.append("WAVEfmt ");
	put32(16);
	put16(1); // PCM
	put16(2);
	put32(FileRate);
	put32(FileRate * 4);
	put16(4);
	put16(16);
	data.append("data");
	put32(frames * 4);
	for (int i = 0; i < frames; ++i)
	{
		const auto v = static_cast<qint16>(16000 * std::sin(2 * std::numbers::pi * frequency * i / FileRate));
		put16(v);
		put16(v);
	}
	QFile file(path);
	QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
	file.write(data);
}

QDomElement save(SampleClip* clip, QDomDocument& doc)
{
	auto element = doc.createElement(clip->nodeName());
	clip->saveSettings(doc, element);
	return element;
}

SampleClip* copyOf(SampleClip* clip, Track* track)
{
	QDomDocument doc;
	auto copy = new SampleClip(track);
	copy->loadSettings(save(clip, doc));
	return copy;
}

} // namespace


class SampleClipCacheTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase() { Engine::init(true); }
	void cleanupTestCase() { Engine::destroy(); }

	void init()
	{
		QVERIFY(m_dir.isValid());
		m_path = m_dir.filePath(QString("loop%1.wav").arg(m_fileIndex++));
		writeWav(m_path, FileRate, 440.f);
	}

	void copyOfPlainClipSharesBuffer()
	{
		SampleTrack track(Engine::getSong());
		auto clip = new SampleClip(&track);
		clip->setSampleFile(m_path);
		QCOMPARE(clip->sample().sampleSize(), static_cast<std::size_t>(FileRate));

		auto copy = copyOf(clip, &track);
		QCOMPARE(copy->sample().buffer(), clip->sample().buffer());
		QCOMPARE(copy->sampleFile(), clip->sampleFile());

		// Loading the same file afresh (e.g. dropping it from the browser again) shares it too
		auto another = new SampleClip(&track);
		another->setSampleFile(m_path);
		QCOMPARE(another->sample().buffer(), clip->sample().buffer());
	}

	void copyOfStretchedClipSharesBuffer()
	{
		SampleTrack track(Engine::getSong());
		auto clip = new SampleClip(&track);
		clip->setSampleFile(m_path);
		clip->resample(1.25, true);
		QCOMPARE(clip->sample().sampleRate(), static_cast<sample_rate_t>(FileRate));
		QCOMPARE(clip->sample().sampleSize(), static_cast<std::size_t>(std::llround(FileRate / 1.25)));

		auto copy = copyOf(clip, &track);
		QCOMPARE(copy->sample().buffer(), clip->sample().buffer());

		// A second stretch is still exact and still shared
		clip->resample(1.1, true);
		auto copy2 = copyOf(clip, &track);
		QCOMPARE(copy2->sample().buffer(), clip->sample().buffer());
		QVERIFY(copy2->sample().buffer() != copy->sample().buffer());
	}

	void copyOfResampledClipSharesBuffer()
	{
		SampleTrack track(Engine::getSong());
		auto clip = new SampleClip(&track);
		clip->setSampleFile(m_path);
		clip->resample(1.1, false);
		clip->resample(1.3, true);
		const auto rate = clip->sample().sampleRate();
		QVERIFY(rate != static_cast<sample_rate_t>(FileRate));

		auto copy = copyOf(clip, &track);
		QCOMPARE(copy->sample().buffer(), clip->sample().buffer());
		QCOMPARE(copy->sample().sampleRate(), rate);
	}

	void freshLoadMatchesCachedLoad()
	{
		SampleTrack track(Engine::getSong());
		auto clip = new SampleClip(&track);
		clip->setSampleFile(m_path);
		clip->resample(1.1, false);
		clip->resample(0.8, true);
		const auto size = clip->sample().sampleSize();
		const auto rate = clip->sample().sampleRate();

		QDomDocument doc;
		const auto element = save(clip, doc);
		std::weak_ptr<const SampleBuffer> held = clip->sample().buffer();
		delete clip;
		// The cache never keeps audio alive on its own
		QVERIFY(held.expired());

		auto loaded = new SampleClip(&track);
		loaded->loadSettings(element);
		QCOMPARE(loaded->sample().sampleSize(), size);
		QCOMPARE(loaded->sample().sampleRate(), rate);

		// And what it saves is exactly what it loaded
		QDomDocument doc2;
		const auto resaved = save(loaded, doc2);
		QCOMPARE(resaved.attribute("stretch"), element.attribute("stretch"));
		QCOMPARE(resaved.attribute("resample"), element.attribute("resample"));
	}

	void changedFileIsNotReused()
	{
		SampleTrack track(Engine::getSong());
		auto clip = new SampleClip(&track);
		clip->setSampleFile(m_path);

		// Different length
		writeWav(m_path, FileRate / 2, 440.f);
		auto shorter = new SampleClip(&track);
		shorter->setSampleFile(m_path);
		QVERIFY(shorter->sample().buffer() != clip->sample().buffer());
		QCOMPARE(shorter->sample().sampleSize(), static_cast<std::size_t>(FileRate / 2));

		// Same size, different content: only the modification time tells them apart
		writeWav(m_path, FileRate / 2, 880.f);
		QFile file(m_path);
		QVERIFY(file.open(QIODevice::ReadWrite));
		QVERIFY(file.setFileTime(QDateTime::currentDateTime().addSecs(10), QFileDevice::FileModificationTime));
		file.close();
		auto changed = copyOf(shorter, &track);
		QVERIFY(changed->sample().buffer() != shorter->sample().buffer());
		QVERIFY(changed->sample().buffer()->data()[10].left() != shorter->sample().buffer()->data()[10].left());
	}

	void copiesKeepTheirOwnReverseAndGain()
	{
		SampleTrack track(Engine::getSong());
		auto clip = new SampleClip(&track);
		clip->setSampleFile(m_path);
		auto copy = copyOf(clip, &track);
		QCOMPARE(copy->sample().buffer(), clip->sample().buffer());

		copy->sample().setReversed(true);
		copy->setGain(0.25f);
		QVERIFY(!clip->sample().reversed());
		QCOMPARE(clip->gain(), 1.f);

		auto copyOfCopy = copyOf(copy, &track);
		QVERIFY(copyOfCopy->sample().reversed());
		QCOMPARE(copyOfCopy->gain(), 0.25f);
		QCOMPARE(copyOfCopy->sample().buffer(), clip->sample().buffer());
	}

	void loadProgressIsReported()
	{
		SampleTrack track(Engine::getSong());
		auto clip = new SampleClip(&track);
		clip->setSampleFile(m_path);
		clip->resample(1.25, true);
		QDomDocument doc;
		const auto element = save(clip, doc);
		delete clip;

		float last = -1.f;
		int calls = 0;
		SampleClip::setLoadProgress([&](float done) {
			QVERIFY(done >= last && done <= 1.f);
			last = done;
			++calls;
		});
		auto loaded = new SampleClip(&track);
		loaded->loadSettings(element);
		SampleClip::setLoadProgress({});
		QVERIFY(calls > 0);

		// The second copy comes from the cache, so there is nothing to stretch or report
		calls = 0;
		SampleClip::setLoadProgress([&](float) { ++calls; });
		auto copy = copyOf(loaded, &track);
		SampleClip::setLoadProgress({});
		QCOMPARE(calls, 0);
		QCOMPARE(copy->sample().buffer(), loaded->sample().buffer());
	}

private:
	QTemporaryDir m_dir;
	QString m_path;
	int m_fileIndex = 0;
};

QTEST_GUILESS_MAIN(SampleClipCacheTest)
#include "SampleClipCacheTest.moc"
