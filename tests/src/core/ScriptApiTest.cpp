/* Copyright (c) 2026 LMMS developers. SPDX-License-Identifier: GPL-2.0-or-later */
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTcpSocket>

#include "ConfigManager.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Mixer.h"
#include "NotePlayHandle.h"
#include "PatternStore.h"
#include "ScriptApi.h"
#include "ScriptServer.h"
#include "Song.h"

using namespace lmms;
using scripting::ScriptApi;

class ScriptApiTest : public QObject
{
	Q_OBJECT
	QTemporaryDir m_configDir;

	//! Synchronous call that fails the test on errors
	QJsonValue call(const QString& method, const QJsonObject& params = {})
	{
		QJsonValue result;
		QString error = "no reply";
		ScriptApi::instance()->call(method, params, [&](const QJsonValue& r, const QString& e) {
			result = r;
			error = e;
		});
		if (!error.isEmpty()) { qWarning() << method << error; }
		return error.isEmpty() ? result : QJsonValue(QJsonValue::Undefined);
	}

	QString callError(const QString& method, const QJsonObject& params = {})
	{
		QString error;
		ScriptApi::instance()->call(method, params, [&](const QJsonValue&, const QString& e) { error = e; });
		return error;
	}

private slots:
	void initTestCase()
	{
		// a private config, so saving projects doesn't touch the developer's settings
		QVERIFY(m_configDir.isValid());
		ConfigManager::inst()->loadConfigFile(m_configDir.path() + "/lmmsrc.xml");
		ConfigManager::inst()->setWorkingDir(m_configDir.path() + "/work/");
		NotePlayHandleManager::init();
		Engine::init(true);
	}

	void cleanupTestCase()
	{
		scripting::ScriptServer::instance()->stop();
		Engine::destroy();
		NotePlayHandleManager::free();
	}

	void emptyProjectHasNoTracks()
	{
		QVERIFY(call("song.new", {{"empty", true}}).toBool());
		QCOMPARE(Engine::getSong()->tracks().size(), std::size_t{0});
		QCOMPARE(Engine::patternStore()->tracks().size(), std::size_t{0});
		call("song.set", {{"tempo", 123}, {"time_signature", QJsonArray{3, 4}}});
		const auto info = call("song.info").toObject();
		QCOMPARE(info["tempo"].toInt(), 123);
		QCOMPARE(info["time_signature"].toArray()[0].toInt(), 3);
		QCOMPARE(info["beats_per_bar"].toDouble(), 3.0);
		call("song.set", {{"time_signature", QJsonArray{4, 4}}});
	}

	void tracksClipsAndNotes()
	{
		call("song.new", {{"empty", true}});
		const auto track = call("song.add_track", {{"type", "instrument"}, {"instrument", "tripleoscillator"},
			{"name", "Lead"}}).toObject();
		QCOMPARE(track["name"].toString(), QString("Lead"));
		QCOMPARE(track["instrument"].toObject()["name"].toString(), QString("tripleoscillator"));

		// beats -> ticks, note names, arrays and objects, velocity/pan
		const auto clip = call("track.add_clip", {{"id", track["id"]}, {"start", 4}, {"length", 8},
			{"notes", QJsonArray{QJsonArray{"C4", 0, 1}, QJsonObject{{"pitch", 64}, {"start", 1.5}, {"length", 0.5},
				{"velocity", 150}, {"pan", -50}}}}}).toObject();
		QCOMPARE(clip["start"].toDouble(), 4.0);
		QCOMPARE(clip["length"].toDouble(), 8.0);
		QCOMPARE(clip["note_count"].toInt(), 2);

		auto mc = dynamic_cast<MidiClip*>(ScriptApi::instance()->objectFor(clip["id"].toInt()));
		QVERIFY(mc);
		QCOMPARE(mc->startPosition().getTicks(), 4 * 48);
		QCOMPARE(mc->notes()[0]->key(), 60);
		QCOMPARE(mc->notes()[1]->key(), 64);
		QCOMPARE(mc->notes()[1]->pos().getTicks(), 72);
		QCOMPARE(static_cast<int>(mc->notes()[1]->getVolume()), 150);
		QCOMPARE(static_cast<int>(mc->notes()[1]->getPanning()), -50);

		const auto notes = call("clip.notes", {{"id", clip["id"]}}).toArray();
		QCOMPARE(notes.size(), 2);
		QCOMPARE(notes[1].toObject()["start"].toDouble(), 1.5);

		call("clip.remove_notes", {{"id", clip["id"]}, {"indices", QJsonArray{0}}});
		QCOMPARE(mc->notes().size(), std::size_t{1});
		call("clip.set_notes", {{"id", clip["id"]}, {"notes", QJsonArray{QJsonArray{"A3", 0, 2}}}});
		QCOMPARE(mc->notes()[0]->key(), 57);

		QVERIFY(callError("clip.add_notes", {{"id", clip["id"]}, {"notes", QJsonArray{QJsonArray{"X9", 0, 1}}}})
			.contains("note name"));

		call("clip.delete", {{"id", clip["id"]}});
		QVERIFY(!ScriptApi::instance()->objectFor(clip["id"].toInt()));
		QVERIFY(callError("clip.info", {{"id", clip["id"]}}).contains("no longer exists"));
	}

	void pluginParamsUseSaveNames()
	{
		call("song.new", {{"empty", true}});
		const auto track = call("song.add_track", {{"instrument", "tripleoscillator"}}).toObject();
		const int instrument = track["instrument"].toObject()["id"].toInt();
		const auto params = call("plugin.params", {{"id", instrument}}).toArray();
		QStringList keys;
		for (const auto& p : params) { keys << p.toObject()["key"].toString(); }
		QVERIFY(keys.contains("vol0"));
		QVERIFY(keys.contains("coarse1"));

		const auto set = call("plugin.set_params", {{"id", instrument}, {"values", QJsonObject{{"vol0", 80},
			{"Osc 2 coarse detuning", 7}}}}).toObject();
		QCOMPARE(set["vol0"].toDouble(), 80.0);
		QCOMPARE(set["Osc 2 coarse detuning"].toInt(), 7);
		QVERIFY(callError("plugin.set_params", {{"id", instrument}, {"values", QJsonObject{{"nope", 1}}}})
			.contains("Available: "));

		// envelope models share save names: they are disambiguated by their parent node
		const auto trackParams = call("track.params", {{"id", track["id"]}}).toArray();
		QStringList trackKeys;
		for (const auto& p : trackParams) { trackKeys << p.toObject()["key"].toString(); }
		QVERIFY(trackKeys.contains("vol"));
		QVERIFY(trackKeys.contains("elvol.att"));
		QVERIFY(trackKeys.contains("elcut.att"));

		// state round trip
		const QString state = call("plugin.get_state", {{"id", instrument}}).toString();
		QVERIFY(state.contains("vol0=\"80\""));
		call("plugin.set_params", {{"id", instrument}, {"values", QJsonObject{{"vol0", 20}}}});
		call("plugin.set_state", {{"id", instrument}, {"xml", state}});
		QCOMPARE(call("plugin.set_params", {{"id", instrument}, {"values", QJsonObject{}}}).toObject().size(), 0);
		const auto after = call("plugin.params", {{"id", instrument}}).toArray();
		for (const auto& p : after)
		{
			if (p.toObject()["key"] == "vol0") { QCOMPARE(p.toObject()["value"].toDouble(), 80.0); }
		}
	}

	void effectsAndMixer()
	{
		call("song.new", {{"empty", true}});
		const auto track = call("song.add_track", {{"instrument", "tripleoscillator"}}).toObject();
		const auto fx = call("track.add_effect", {{"id", track["id"]}, {"plugin", "amplifier"},
			{"params", QJsonObject{{"wet", 0.25}}}}).toObject();
		QCOMPARE(fx["name"].toString(), QString("amplifier"));
		QCOMPARE(fx["wet"].toDouble(), 0.25);
		QCOMPARE(call("track.effects", {{"id", track["id"]}}).toArray().size(), 1);
		call("effect.delete", {{"id", fx["id"]}});
		QCOMPARE(call("track.effects", {{"id", track["id"]}}).toArray().size(), 0);

		const auto channel = call("mixer.add_channel", {{"name", "Bus"}}).toObject();
		const int index = channel["index"].toInt();
		QVERIFY(index > 0);
		const auto updated = call("track.set", {{"id", track["id"]}, {"mixer_channel", index}}).toObject();
		QCOMPARE(updated["mixer_channel"].toInt(), index);
		QVERIFY(callError("mixer.send", {{"from", 0}, {"to", index}, {"amount", 1}}).contains("loop"));
		call("mixer.delete_channel", {{"index", index}});
		QCOMPARE(Engine::mixer()->numChannels(), 1);
	}

	void automationAndPatterns()
	{
		call("song.new", {{"empty", true}});
		const auto track = call("song.add_track", {{"instrument", "tripleoscillator"}}).toObject();
		const auto params = call("track.params", {{"id", track["id"]}}).toArray();
		int volume = 0;
		for (const auto& p : params) { if (p.toObject()["key"] == "vol") { volume = p.toObject()["id"].toInt(); } }
		QVERIFY(volume);

		const auto autoTrack = call("song.add_track", {{"type", "automation"}}).toObject();
		const auto clip = call("track.add_clip", {{"id", autoTrack["id"]}, {"start", 0}, {"target", volume},
			{"points", QJsonArray{QJsonArray{0, 10}, QJsonArray{4, 150}}}}).toObject();
		const auto automation = call("clip.automation", {{"id", clip["id"]}}).toObject();
		QCOMPARE(automation["points"].toArray().size(), 2);
		QCOMPARE(automation["targets"].toArray()[0].toInt(), volume);

		// the function generator: a fade-in sine, then a formula, then a bad request
		const auto gen = call("clip.generate", {{"id", clip["id"]}, {"shape", "sine"}, {"cycles", 4},
			{"amplitude", QJsonArray{QJsonArray{0, 0}, QJsonArray{1, 1}}}, {"low", 0}, {"high", 100}}).toObject();
		QVERIFY(gen["nodes"].toInt() > 16);
		auto pts = call("clip.automation", {{"id", clip["id"]}}).toObject()["points"].toArray();
		QVERIFY(pts.size() > 16);
		QCOMPARE(pts[0].toArray()[1].toDouble(), 50.0);
		call("clip.generate", {{"id", clip["id"]}, {"expression", "50 + 50 * sin(tau * p) * (1 - t)"}, {"cycles", 2}});
		QVERIFY(callError("clip.generate", {{"id", clip["id"]}, {"expression", "sin("}}).contains("expression"));
		QVERIFY(callError("clip.generate", {{"id", clip["id"]}, {"shape", "wobble"}}).contains("shape"));

		const auto pattern = call("song.add_track", {{"type", "pattern"}}).toObject();
		const auto kick = call("song.add_track", {{"instrument", "kicker"}, {"container", "pattern"}}).toObject();
		const auto steps = call("track.pattern_clip", {{"id", kick["id"]}, {"pattern", pattern["pattern_index"]}}).toObject();
		const auto updated = call("clip.set_steps", {{"id", steps["id"]}, {"on", QJsonArray{0, 4, 8, 12}}}).toObject();
		QCOMPARE(updated["note_count"].toInt(), 4);
		QVERIFY(callError("track.add_clip", {{"id", kick["id"]}}).contains("pattern"));
	}

	void batchAndUnknownMethods()
	{
		QVERIFY(callError("nope.nope").contains("unknown method"));
		const auto results = call("api.batch", {{"calls", QJsonArray{
			QJsonObject{{"method", "api.ping"}}, QJsonObject{{"method", "nope"}}}}}).toArray();
		QCOMPARE(results[0].toObject()["result"].toString(), QString("pong"));
		QVERIFY(results[1].toObject().contains("error"));
		QVERIFY(call("api.describe").toObject()["methods"].toObject().contains("song.render"));

		// async methods would reply after the batch returned; they must be refused without running
		const auto refused = call("api.batch", {{"calls", QJsonArray{QJsonObject{{"method", "song.render"},
			{"params", QJsonObject{{"path", m_configDir.path() + "/batch.wav"}}}}}}}).toArray();
		QVERIFY(refused[0].toObject()["error"].toString().contains("asynchronous"));
		QVERIFY(!Engine::getSong()->isExporting());
	}

	void liveNotesRejectInvalidPitches()
	{
		call("song.new", {{"empty", true}});
		const auto track = call("song.add_track", {{"instrument", "tripleoscillator"}}).toObject();
		// out-of-range keys used to index past InstrumentTrack's note table
		QVERIFY(callError("track.note_on", {{"id", track["id"]}, {"pitch", 200}}).contains("outside 0..127"));
		QVERIFY(callError("track.note_off", {{"id", track["id"]}, {"pitch", -1}}).contains("outside 0..127"));
		QVERIFY(callError("track.note_on", {{"id", track["id"]}, {"pitch", "C11"}}).contains("outside 0..127"));
	}

	void serverRequiresToken()
	{
		auto server = scripting::ScriptServer::instance();
		QVERIFY(server->start());
		QVERIFY(QFile::exists(scripting::ScriptServer::connectionFile()));

		// the server lives in this thread: keep its event loop running while waiting
		auto exchange = [](QTcpSocket& socket, const QJsonObject& message) {
			socket.write(QJsonDocument(message).toJson(QJsonDocument::Compact) + "\n");
			socket.flush();
			QElapsedTimer timer;
			timer.start();
			while (!socket.canReadLine() && timer.elapsed() < 5000) { QTest::qWait(5); }
			return QJsonDocument::fromJson(socket.readLine()).object();
		};

		QTcpSocket intruder;
		intruder.connectToHost("127.0.0.1", server->port());
		QTRY_VERIFY(intruder.state() == QAbstractSocket::ConnectedState);
		QVERIFY(exchange(intruder, {{"token", "wrong"}}).contains("error"));

		QTcpSocket client;
		client.connectToHost("127.0.0.1", server->port());
		QTRY_VERIFY(client.state() == QAbstractSocket::ConnectedState);
		QVERIFY(exchange(client, {{"token", server->token()}})["ok"].toBool());
		const auto reply = exchange(client, {{"id", 7}, {"method", "api.ping"}});
		QCOMPARE(reply["id"].toInt(), 7);
		QCOMPARE(reply["result"].toString(), QString("pong"));
		const auto failure = exchange(client, {{"id", 8}, {"method", "track.info"}, {"params", QJsonObject{{"id", 999999}}}});
		QVERIFY(failure["error"].toObject()["message"].toString().contains("no longer exists"));

		server->stop();
		QVERIFY(!QFile::exists(scripting::ScriptServer::connectionFile()));
	}
};

QTEST_GUILESS_MAIN(ScriptApiTest)
#include "ScriptApiTest.moc"
