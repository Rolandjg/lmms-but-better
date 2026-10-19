#include <QtTest>

#include <QDir>
#include <QDomDocument>
#include <QTemporaryDir>

#include "AudioBusHandle.h"
#include "Engine.h"
#include "Mixer.h"
#include "ProjectRenderer.h"
#include "ProjectResources.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"

namespace
{

using namespace lmms;

QByteArray readProject(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return {}; }
	const auto data = file.readAll();
	return path.endsWith(".mmpz") ? qUncompress(data) : data;
}

SampleClip* addConstantClip(SampleTrack* track, float level, tick_t start, tick_t length)
{
	const auto rate = Engine::audioEngine()->outputSampleRate();
	auto clip = new SampleClip(track);
	clip->setSampleBuffer(std::make_shared<SampleBuffer>(std::vector<SampleFrame>(rate * 4, SampleFrame{level}), rate));
	clip->movePosition(TimePos{start});
	clip->changeLength(TimePos{length});
	return clip;
}

struct RenderResult
{
	bool ready = false;
	QString error;
	float peak = 0.f;
};

RenderResult render(const QString& path, TimePos begin, TimePos end, bool tail)
{
	auto engine = Engine::audioEngine();
	RenderResult result;
	engine->storeAudioDevice();
	{
		ProjectRenderer renderer(OutputSettings(engine->outputSampleRate(), 0, OutputSettings::BitDepth::Depth32Bit),
			ProjectRenderer::ExportFileFormat::Wave, path, begin, end);
		result.ready = renderer.isReady();
		renderer.setRenderTail(tail);
		renderer.startProcessing();
		if (result.ready && !renderer.wait(20000)) { renderer.abortProcessing(); }
		result.error = renderer.errorString();
	}
	engine->restoreAudioDevice();
	if (result.ready && QFileInfo::exists(path))
	{
		const auto buffer = SampleBuffer::fromFile(path);
		for (const auto& frame : *buffer)
		{
			result.peak = std::max({result.peak, std::abs(frame.left()), std::abs(frame.right())});
		}
	}
	return result;
}

} // namespace


class ProjectPersistenceTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase() { Engine::init(true); }
	void cleanupTestCase() { Engine::destroy(); }
	void init() { Engine::getSong()->createEmptyProject(); }

	void saveReplacesDestinationAndKeepsBackup()
	{
		QTemporaryDir dir;
		const auto path = dir.filePath("song.mmp");
		auto song = Engine::getSong();
		song->setTempo(111);
		QVERIFY(song->guiSaveProjectAs(path));
		song->setTempo(122);
		QVERIFY(song->guiSaveProjectAs(path));
		QVERIFY(readProject(path).contains("bpm=\"122\""));
		QVERIFY(readProject(path + ".bak").contains("bpm=\"111\""));
		QVERIFY(!QFileInfo::exists(path + ".new"));
	}

	void saveFailureIsReported()
	{
		QTemporaryDir dir;
		const auto path = dir.filePath("blocked.mmp");
		QVERIFY(QDir().mkpath(path));
		QVERIFY(QDir().mkpath(path + ".bak/occupied"));
		auto song = Engine::getSong();
		song->setModified();
		QVERIFY(!song->guiSaveProjectAs(path));
		QVERIFY(song->isModified());
		QVERIFY(QFileInfo(path).isDir());
	}

	void blockedBackupDoesNotBlockSave()
	{
		QTemporaryDir dir;
		const auto path = dir.filePath("song.mmp");
		auto song = Engine::getSong();
		QVERIFY(song->guiSaveProjectAs(path));
		QVERIFY(QDir().mkpath(path + ".bak/occupied"));
		song->setTempo(133);
		QVERIFY(song->guiSaveProjectAs(path));
		QVERIFY(readProject(path).contains("bpm=\"133\""));
	}

	void unnamedSaveIsRefused()
	{
		QTemporaryDir dir;
		const auto previous = QDir::currentPath();
		QDir::setCurrent(dir.path());
		auto song = Engine::getSong();
		const bool saved = song->guiSaveProject();
		QDir::setCurrent(previous);
		QVERIFY(!saved);
		QVERIFY(song->projectFileName().isEmpty());
		QVERIFY(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden).isEmpty());
	}

	void headlessRoundTripKeepsMarkersAndEditorState()
	{
		QTemporaryDir dir;
		const auto path = dir.filePath("arranged.mmp");
		auto song = Engine::getSong();
		song->getTimeline(Song::PlayMode::Song).addMarker(TimePos{192}, "Chorus", QColor("#336699"));
		QVERIFY(song->guiSaveProjectAs(path));
		QVERIFY(readProject(path).contains("Chorus"));

		// add GUI-only state (normally written by the editors) and check a headless open/save keeps it
		auto xml = QString::fromUtf8(readProject(path));
		xml.replace("</song>", "<projectnotes visible=\"0\"><![CDATA[remember the bridge]]></projectnotes></song>");
		{
			QFile file(path);
			QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
			file.write(xml.toUtf8());
		}

		song->createEmptyProject();
		QCOMPARE(song->getTimeline(Song::PlayMode::Song).markers().size(), std::size_t{0});
		song->loadProject(path);
		const auto& markers = song->getTimeline(Song::PlayMode::Song).markers();
		QCOMPARE(markers.size(), std::size_t{1});
		QCOMPARE(markers.front().name, QString("Chorus"));
		QCOMPARE(markers.front().pos.getTicks(), 192);

		QVERIFY(song->guiSaveProject());
		const auto saved = readProject(path);
		QVERIFY(saved.contains("Chorus"));
		QVERIFY(saved.contains("remember the bridge"));

		// a different project must not inherit that state
		song->createEmptyProject();
		const auto other = dir.filePath("other.mmp");
		QVERIFY(song->guiSaveProjectAs(other));
		QVERIFY(!readProject(other).contains("remember the bridge"));
	}

	void exportOpenErrorIsReturned()
	{
		QTemporaryDir dir;
		const auto path = dir.filePath("blocked.wav");
		QVERIFY(QDir().mkpath(path));
		const auto result = render(path, TimePos{0}, TimePos{48}, false);
		QVERIFY(!result.ready);
		QVERIFY(!result.error.isEmpty());
	}

	void tailDoesNotStartLaterClips()
	{
		auto song = Engine::getSong();
		auto track = new SampleTrack(song);
		addConstantClip(track, 0.1f, 0, 48);
		addConstantClip(track, 0.7f, 48, 48);
		QTemporaryDir dir;
		const auto result = render(dir.filePath("range.wav"), TimePos{0}, TimePos{48}, true);
		QVERIFY(result.ready);
		QVERIFY(result.error.isEmpty());
		QVERIFY(result.peak > 0.05f);
		QVERIFY2(result.peak < 0.2f, qPrintable(QString("peak %1").arg(result.peak)));
	}

	void resourcesAreListed()
	{
		QTemporaryDir dir;
		const auto projectFile = dir.filePath("song.mmpz");
		{
			QFile bounce(dir.filePath("lead-bounce-abc123.wav"));
			QVERIFY(bounce.open(QIODevice::WriteOnly));
		}
		QDomDocument project;
		project.setContent(QStringLiteral(
			"<lmms-project><song>"
			"<sampleclip src=\"lead-bounce-abc123.wav\"/><sampleclip src=\"lead-bounce-abc123.wav\"/>"
			"<sampleclip src=\"/nowhere/gone.wav\"/>"
			"<instrument name=\"tripleoscillator\"/><instrument name=\"tripleoscillator\"/>"
			"<effect name=\"amplifier\"/>"
			"</song></lmms-project>"));
		const auto resources = ProjectResources::scan(project, projectFile);
		QCOMPARE(resources.files.size(), std::size_t{2});
		const auto& bounce = resources.files[0];
		QCOMPARE(bounce.kind, ProjectResources::File::Kind::Bounce);
		QCOMPARE(bounce.location, ProjectResources::File::Location::ProjectFolder);
		QCOMPARE(bounce.uses, 2);
		QVERIFY(!bounce.missing);
		const auto& gone = resources.files[1];
		QVERIFY(gone.missing);
		QCOMPARE(gone.location, ProjectResources::File::Location::Elsewhere);
		QCOMPARE(resources.missingCount(), 1);
		QCOMPARE(resources.nonPortableCount(), 1);
		QCOMPARE(resources.instruments.at("tripleoscillator"), 2);
		QCOMPARE(resources.effects.at("amplifier"), 1);
	}

	void bounceTapSkipsMasterSection()
	{
		auto song = Engine::getSong();
		auto track = new SampleTrack(song);
		addConstantClip(track, 0.4f, 0, 96);
		song->setMasterVolume(50);
		QTemporaryDir dir;
		const auto plain = render(dir.filePath("plain.wav"), TimePos{0}, TimePos{96}, false);

		auto engine = Engine::audioEngine();
		engine->requestChangeInModel();
		Engine::mixer()->setBounceTap({track->audioBusHandle()});
		engine->doneChangeInModel();
		const auto tapped = render(dir.filePath("tapped.wav"), TimePos{0}, TimePos{96}, false);
		engine->requestChangeInModel();
		Engine::mixer()->clearBounceTap();
		engine->doneChangeInModel();
		song->setMasterVolume(100);

		QVERIFY(plain.ready && tapped.ready);
		QVERIFY2(std::abs(plain.peak - 0.2f) < 0.01f, qPrintable(QString("plain %1").arg(plain.peak)));
		QVERIFY2(std::abs(tapped.peak - 0.4f) < 0.01f, qPrintable(QString("tapped %1").arg(tapped.peak)));
	}
};

QTEST_GUILESS_MAIN(ProjectPersistenceTest)
#include "ProjectPersistenceTest.moc"
