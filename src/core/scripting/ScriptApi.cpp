/*
 * ScriptApi.cpp - the command set exposed to scripts (Python module "lmms", agents, ...)
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#include "ScriptApi.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

#include <QCoreApplication>
#include <QDir>
#include <QDomDocument>
#include <QFileInfo>
#include <QJsonArray>
#include <QPixmap>
#include <QRegularExpression>
#include <QTimer>
#include <QWidget>

#include "AudioEngine.h"
#include "AutomatableModel.h"
#include "AutomationClip.h"
#include "AutomationTrack.h"
#include "ComboBoxModel.h"
#include "ConfigManager.h"
#include "DataFile.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "ImportFilter.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "MainWindow.h"
#include "MidiClip.h"
#include "Mixer.h"
#include "MixerView.h"
#include "Note.h"
#include "PatternClip.h"
#include "PatternEditor.h"
#include "PatternStore.h"
#include "PatternTrack.h"
#include "PianoRoll.h"
#include "AutomationEditor.h"
#include "PluginFactory.h"
#include "ProjectRenderer.h"
#include "RenderManager.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "ScriptConsole.h"
#include "Song.h"
#include "SongEditor.h"
#include "TrackContainerView.h"
#include "TrackView.h"
#include "lmmsversion.h"

namespace lmms::scripting
{

namespace
{

constexpr int ApiVersion = 1;
constexpr double TicksPerBeat = DefaultTicksPerBar / 4.0;

int toTicks(double beats) { return static_cast<int>(std::lround(beats * TicksPerBeat)); }
double toBeats(int ticks) { return ticks / TicksPerBeat; }

QString slug(const QString& text)
{
	QString out = text.toLower();
	out.replace(QRegularExpression("[^a-z0-9]+"), "_");
	while (out.startsWith('_')) { out.remove(0, 1); }
	while (out.endsWith('_')) { out.chop(1); }
	return out;
}


// ---------------------------------------------------------------- parameter access

bool has(const QJsonObject& p, const char* key) { return p.contains(key) && !p.value(key).isNull(); }

double num(const QJsonObject& p, const char* key)
{
	if (!has(p, key)) { throw ScriptError(QString("missing parameter '%1'").arg(key)); }
	const auto v = p.value(key);
	if (v.isBool()) { return v.toBool() ? 1. : 0.; }
	if (!v.isDouble()) { throw ScriptError(QString("parameter '%1' must be a number").arg(key)); }
	return v.toDouble();
}

double num(const QJsonObject& p, const char* key, double fallback) { return has(p, key) ? num(p, key) : fallback; }

QString str(const QJsonObject& p, const char* key)
{
	if (!has(p, key)) { throw ScriptError(QString("missing parameter '%1'").arg(key)); }
	if (!p.value(key).isString()) { throw ScriptError(QString("parameter '%1' must be a string").arg(key)); }
	return p.value(key).toString();
}

QString str(const QJsonObject& p, const char* key, const QString& fallback) { return has(p, key) ? str(p, key) : fallback; }

bool flag(const QJsonObject& p, const char* key, bool fallback)
{
	if (!has(p, key)) { return fallback; }
	const auto v = p.value(key);
	return v.isBool() ? v.toBool() : v.toDouble() != 0.;
}

//! Expand "~" and make relative paths relative to the current directory of LMMS
QString filePath(const QString& path)
{
	QString out = path;
	if (out.startsWith("~/")) { out = QDir::homePath() + out.mid(1); }
	return QFileInfo(out).absoluteFilePath();
}

//! MIDI note number from 60, "60", "C4", "c#3", "Eb5" (C4 = 60)
int parsePitch(const QJsonValue& v)
{
	if (v.isDouble()) { return static_cast<int>(std::lround(v.toDouble())); }
	if (!v.isString()) { throw ScriptError("pitch must be a MIDI number or a note name like \"C4\""); }
	const QString s = v.toString().trimmed();
	bool ok = false;
	const int n = s.toInt(&ok);
	if (ok) { return n; }
	static const QRegularExpression re("^([A-Ga-g])([#b]?)(-?\\d+)$");
	const auto m = re.match(s);
	if (!m.hasMatch()) { throw ScriptError(QString("cannot parse note name \"%1\" (expected e.g. C4, F#2, Bb3)").arg(s)); }
	static const int base[] = {9, 11, 0, 2, 4, 5, 7}; // A B C D E F G
	int semis = base[m.captured(1).toUpper().at(0).unicode() - 'A'];
	if (m.captured(2) == "#") { ++semis; }
	if (m.captured(2) == "b") { --semis; }
	return (m.captured(3).toInt() + 1) * 12 + semis;
}

//! parsePitch() limited to the keys LMMS can play (0..127)
int parseKey(const QJsonValue& v)
{
	const int key = parsePitch(v);
	if (key < 0 || key >= NumKeys) { throw ScriptError(QString("pitch %1 is outside 0..127").arg(key)); }
	return key;
}

QJsonValue colorJson(const std::optional<QColor>& c)
{
	return c ? QJsonValue(c->name()) : QJsonValue();
}

std::optional<QColor> parseColor(const QJsonValue& v)
{
	if (v.isNull() || v.toString().isEmpty()) { return std::nullopt; }
	QColor c(v.toString());
	if (!c.isValid()) { throw ScriptError(QString("invalid color \"%1\" (use e.g. \"#ff8800\")").arg(v.toString())); }
	return c;
}


// ---------------------------------------------------------------- object descriptions

QString trackTypeName(Track::Type t)
{
	switch (t)
	{
	case Track::Type::Instrument: return "instrument";
	case Track::Type::Pattern: return "pattern";
	case Track::Type::Sample: return "sample";
	case Track::Type::Automation: return "automation";
	case Track::Type::HiddenAutomation: return "hidden_automation";
	default: return "other";
	}
}

Track::Type parseTrackType(const QString& s)
{
	if (s == "instrument") { return Track::Type::Instrument; }
	if (s == "sample" || s == "audio") { return Track::Type::Sample; }
	if (s == "automation") { return Track::Type::Automation; }
	if (s == "pattern" || s == "beat") { return Track::Type::Pattern; }
	throw ScriptError(QString("unknown track type \"%1\" (instrument, sample, automation, pattern)").arg(s));
}

TrackContainer* container(const QString& name)
{
	if (name.isEmpty() || name == "song") { return Engine::getSong(); }
	if (name == "pattern" || name == "patterns") { return Engine::patternStore(); }
	throw ScriptError(QString("unknown container \"%1\" (song, pattern)").arg(name));
}

QString clipTypeName(const Clip* clip)
{
	if (dynamic_cast<const MidiClip*>(clip)) { return "midi"; }
	if (dynamic_cast<const SampleClip*>(clip)) { return "sample"; }
	if (dynamic_cast<const AutomationClip*>(clip)) { return "automation"; }
	if (dynamic_cast<const PatternClip*>(clip)) { return "pattern"; }
	return "other";
}

EffectChain* effectChainOf(Track* track)
{
	if (auto it = dynamic_cast<InstrumentTrack*>(track)) { return it->audioBusHandle()->effects(); }
	if (auto st = dynamic_cast<SampleTrack*>(track)) { return st->audioBusHandle()->effects(); }
	return nullptr;
}

MixerChannel* mixerChannel(const QJsonObject& p, const char* key = "index")
{
	const int idx = static_cast<int>(num(p, key));
	if (idx < 0 || idx >= Engine::mixer()->numChannels())
	{
		throw ScriptError(QString("mixer channel %1 does not exist (0 = master, %2 channels)")
			.arg(idx).arg(Engine::mixer()->numChannels()));
	}
	return Engine::mixer()->mixerChannel(idx);
}

gui::GuiApplication* gui() { return gui::getGUI(); }

void markModified() { Engine::getSong()->setModified(); }

//! Let every track's mixer channel selector reach all channels (the mixer view does this in the GUI)
void updateMixerChannelRanges()
{
	const int last = Engine::mixer()->numChannels() - 1;
	for (auto tc : std::vector<TrackContainer*>{Engine::getSong(), Engine::patternStore()})
	{
		for (auto track : tc->tracks())
		{
			if (auto it = dynamic_cast<InstrumentTrack*>(track)) { it->mixerChannelModel()->setRange(0, last, 1); }
			else if (auto st = dynamic_cast<SampleTrack*>(track)) { st->mixerChannelModel()->setRange(0, last, 1); }
		}
	}
}


// ---------------------------------------------------------------- parameters (AutomatableModels)

//! The models that belong to @p owner itself (not to nested plugins or clips)
std::vector<AutomatableModel*> ownedModels(QObject* owner)
{
	std::vector<AutomatableModel*> out;
	for (auto model : owner->findChildren<AutomatableModel*>())
	{
		bool foreign = false;
		for (QObject* p = model->parent(); p && p != owner; p = p->parent())
		{
			if (dynamic_cast<Plugin*>(p) || dynamic_cast<Clip*>(p)) { foreign = true; break; }
		}
		if (!foreign) { out.push_back(model); }
	}
	return out;
}

//! saveState() that also works for objects whose journalling is switched off
QDomElement saveObject(JournallingObject* object, QDomDocument& doc, QDomElement& parent)
{
	object->saveJournallingState(true);
	QDomElement element = object->saveState(doc, parent);
	object->restoreJournallingState();
	return element;
}

//! Run the owner's saveSettings() once, remembering the key each of its models is stored under
void harvestSaveNames(QObject* owner)
{
	if (owner->property("_lmmsScriptHarvested").toBool()) { return; }
	QDomDocument doc;
	QDomElement element = doc.createElement("scriptprobe");
	doc.appendChild(element);
	struct Saved
	{
		QPointer<AutomatableModel> model;
		QDomElement parent;
		QString name;
	};
	std::vector<Saved> saved;
	{
		AutomatableModel::SaveNameObserver observer([&saved](AutomatableModel* model, const QDomElement& parent,
			const QString& name) { saved.push_back({model, parent, name}); });
		if (auto object = dynamic_cast<JournallingObject*>(owner)) { saveObject(object, doc, element); }
	}
	// read the parent names only now: some objects rename their element after saving into it
	for (const auto& s : saved)
	{
		if (!s.model) { continue; }
		s.model->setProperty("_lmmsSaveName", s.name);
		s.model->setProperty("_lmmsSaveParent", s.parent.nodeName());
	}
	owner->setProperty("_lmmsScriptHarvested", true);
}

struct ParamEntry
{
	AutomatableModel* model;
	QString key;
};

std::vector<ParamEntry> paramsOf(QObject* owner)
{
	harvestSaveNames(owner);
	const auto models = ownedModels(owner);

	std::vector<ParamEntry> entries;
	QHash<QString, int> baseCount;
	for (auto m : models)
	{
		QString base = m->property("_lmmsSaveName").toString();
		if (base.isEmpty()) { base = slug(m->displayName()); }
		if (base.isEmpty()) { base = "param"; }
		entries.push_back({m, base});
		++baseCount[base];
	}
	// disambiguate keys like "att" that appear in several envelopes by their parent node ("elcut.att")
	for (auto& e : entries)
	{
		const QString parent = e.model->property("_lmmsSaveParent").toString();
		if (baseCount[e.key] > 1 && !parent.isEmpty() && parent != "scriptprobe") { e.key = parent + "." + e.key; }
	}
	QHash<QString, int> seen;
	for (auto& e : entries)
	{
		const int n = ++seen[e.key];
		if (n > 1) { e.key += "_" + QString::number(n); }
	}
	return entries;
}

QString paramType(const AutomatableModel* m)
{
	if (dynamic_cast<const ComboBoxModel*>(m)) { return "choice"; }
	if (dynamic_cast<const BoolModel*>(m)) { return "bool"; }
	if (dynamic_cast<const IntModel*>(m)) { return "int"; }
	return "float";
}

QJsonValue paramValue(const AutomatableModel* m)
{
	const QString type = paramType(m);
	if (type == "bool") { return m->value<bool>(); }
	if (type == "int" || type == "choice") { return m->value<int>(); }
	return m->value<float>();
}

QJsonObject describeParam(ScriptApi* api, AutomatableModel* m, const QString& key)
{
	QJsonObject o;
	o["id"] = api->handleFor(m);
	o["kind"] = "param";
	o["key"] = key;
	o["name"] = m->displayName();
	o["type"] = paramType(m);
	o["value"] = paramValue(m);
	o["min"] = m->minValue<float>();
	o["max"] = m->maxValue<float>();
	o["step"] = m->step<float>();
	o["default"] = m->initValue<float>();
	o["display"] = m->displayValue(m->value<float>());
	o["automated"] = m->isAutomated();
	if (auto combo = dynamic_cast<ComboBoxModel*>(m))
	{
		QJsonArray options;
		for (int i = 0; i < combo->size(); ++i) { options.append(combo->itemText(i)); }
		o["options"] = options;
		o["text"] = combo->currentText();
	}
	return o;
}

void setParamValue(AutomatableModel* m, const QJsonValue& v)
{
	if (auto combo = dynamic_cast<ComboBoxModel*>(m); combo && v.isString())
	{
		const int idx = combo->findText(v.toString());
		if (idx < 0)
		{
			QStringList options;
			for (int i = 0; i < combo->size(); ++i) { options << combo->itemText(i); }
			throw ScriptError(QString("\"%1\" is not an option of %2 (options: %3)")
				.arg(v.toString(), m->displayName(), options.join(", ")));
		}
		m->setValue(idx);
		return;
	}
	if (v.isBool()) { m->setValue(v.toBool() ? 1.f : 0.f); return; }
	if (!v.isDouble()) { throw ScriptError(QString("value for %1 must be a number").arg(m->displayName())); }
	m->setValue(static_cast<float>(v.toDouble()));
}

AutomatableModel* findParam(QObject* owner, const QString& name)
{
	const auto entries = paramsOf(owner);
	for (const auto& e : entries) { if (e.key == name) { return e.model; } }
	for (const auto& e : entries) { if (e.key.compare(name, Qt::CaseInsensitive) == 0) { return e.model; } }
	for (const auto& e : entries) { if (e.model->displayName().compare(name, Qt::CaseInsensitive) == 0) { return e.model; } }
	const QString wanted = slug(name);
	for (const auto& e : entries) { if (slug(e.model->displayName()) == wanted) { return e.model; } }
	QStringList keys;
	for (const auto& e : entries) { keys << e.key; }
	throw ScriptError(QString("no parameter \"%1\". Available: %2").arg(name, keys.join(", ")));
}

QJsonArray describeParams(ScriptApi* api, QObject* owner)
{
	QJsonArray out;
	for (const auto& e : paramsOf(owner)) { out.append(describeParam(api, e.model, e.key)); }
	return out;
}

//! Apply {"key": value, ...} to the parameters of @p owner
void applyParams(QObject* owner, const QJsonObject& values)
{
	for (auto it = values.begin(); it != values.end(); ++it) { setParamValue(findParam(owner, it.key()), it.value()); }
}


// ---------------------------------------------------------------- plugins

const Plugin::Descriptor* findDescriptor(const QString& name, Plugin::Type type)
{
	for (const auto desc : getPluginFactory()->descriptors(type))
	{
		if (name.compare(desc->name, Qt::CaseInsensitive) == 0
			|| name.compare(desc->displayName, Qt::CaseInsensitive) == 0)
		{
			return desc;
		}
	}
	QStringList names;
	for (const auto desc : getPluginFactory()->descriptors(type)) { names << desc->name; }
	throw ScriptError(QString("no %1 plugin \"%2\". Available: %3")
		.arg(type == Plugin::Type::Instrument ? "instrument" : "effect", name, names.join(", ")));
}

Plugin::Descriptor::SubPluginFeatures::KeyList subPluginKeys(const Plugin::Descriptor* desc)
{
	Plugin::Descriptor::SubPluginFeatures::KeyList keys;
	if (desc->subPluginFeatures) { desc->subPluginFeatures->listSubPluginKeys(desc, keys); }
	return keys;
}

//! Build the key for sub-plugins (LADSPA, LV2, VST3...). @p spec is a name or {"name":..., "attributes": {...}}
Plugin::Descriptor::SubPluginFeatures::Key pluginKey(const Plugin::Descriptor* desc, const QJsonValue& spec)
{
	using Key = Plugin::Descriptor::SubPluginFeatures::Key;
	if (spec.isNull() || spec.isUndefined()) { return Key(desc, desc->name); }
	if (spec.isObject())
	{
		const auto o = spec.toObject();
		Key::AttributeMap attributes;
		const auto a = o.value("attributes").toObject();
		for (auto it = a.begin(); it != a.end(); ++it) { attributes[it.key()] = it.value().toString(); }
		return Key(desc, o.value("name").toString(desc->name), attributes);
	}
	const QString name = spec.toString();
	const auto keys = subPluginKeys(desc);
	for (const auto& k : keys) { if (k.name.compare(name, Qt::CaseInsensitive) == 0) { return k; } }
	for (const auto& k : keys) { if (k.name.contains(name, Qt::CaseInsensitive)) { return k; } }
	QStringList names;
	for (const auto& k : keys) { names << k.name; }
	throw ScriptError(QString("%1 has no sub-plugin \"%2\". Available: %3")
		.arg(desc->name, name, names.join(", ")));
}

QJsonObject describeKey(const Plugin::Descriptor::SubPluginFeatures::Key& k)
{
	QJsonObject o;
	o["name"] = k.name;
	QJsonObject attributes;
	for (auto it = k.attributes.begin(); it != k.attributes.end(); ++it) { attributes[it.key()] = it.value(); }
	o["attributes"] = attributes;
	return o;
}

QJsonObject describePlugin(ScriptApi* api, Plugin* plugin)
{
	QJsonObject o;
	o["id"] = api->handleFor(plugin);
	o["kind"] = plugin->type() == Plugin::Type::Instrument ? "instrument" : "effect";
	o["name"] = plugin->descriptor() ? QString(plugin->descriptor()->name) : QString();
	o["display_name"] = plugin->displayName();
	if (plugin->key().isValid() && plugin->descriptor() && plugin->descriptor()->subPluginFeatures)
	{
		o["key"] = describeKey(plugin->key());
	}
	if (auto effect = dynamic_cast<Effect*>(plugin))
	{
		o["enabled"] = effect->isEnabled();
		o["wet"] = effect->wetLevel();
	}
	if (auto instrument = dynamic_cast<Instrument*>(plugin))
	{
		o["track"] = api->handleFor(instrument->instrumentTrack());
	}
	return o;
}

Effect* addEffect(EffectChain* chain, const QJsonObject& p)
{
	const auto desc = findDescriptor(str(p, "plugin"), Plugin::Type::Effect);
	auto key = pluginKey(desc, p.value("key"));
	Effect* effect = Effect::instantiate(desc->name, chain, &key);
	if (!effect) { throw ScriptError(QString("could not load effect \"%1\"").arg(desc->name)); }
	chain->appendEffect(effect);
	if (has(p, "params")) { applyParams(effect, p.value("params").toObject()); }
	markModified();
	return effect;
}

QJsonArray describeChain(ScriptApi* api, EffectChain* chain)
{
	QJsonArray out;
	if (!chain) { return out; }
	for (auto effect : chain->effects()) { out.append(describePlugin(api, effect)); }
	return out;
}


// ---------------------------------------------------------------- tracks & clips

QJsonObject describeTrack(ScriptApi* api, Track* track)
{
	QJsonObject o;
	o["id"] = api->handleFor(track);
	o["kind"] = "track";
	o["type"] = trackTypeName(track->type());
	o["name"] = track->name();
	o["muted"] = track->isMuted();
	o["solo"] = track->isSolo();
	o["color"] = colorJson(track->color());
	o["container"] = track->trackContainer() == Engine::patternStore() ? "pattern" : "song";
	const auto& tracks = track->trackContainer()->tracks();
	o["index"] = static_cast<int>(std::find(tracks.begin(), tracks.end(), track) - tracks.begin());
	o["clip_count"] = track->numOfClips();
	if (auto it = dynamic_cast<InstrumentTrack*>(track))
	{
		if (it->instrument()) { o["instrument"] = describePlugin(api, it->instrument()); }
		o["volume"] = it->volumeModel()->value();
		o["panning"] = it->panningModel()->value();
		o["pitch"] = it->pitchModel()->value();
		o["mixer_channel"] = it->mixerChannelModel()->value();
	}
	if (auto st = dynamic_cast<SampleTrack*>(track))
	{
		o["mixer_channel"] = st->mixerChannelModel()->value();
		o["volume"] = findParam(st, "vol")->value<float>();
		o["panning"] = findParam(st, "pan")->value<float>();
	}
	if (auto pt = dynamic_cast<PatternTrack*>(track)) { o["pattern_index"] = pt->patternIndex(); }
	return o;
}

QJsonObject describeClip(ScriptApi* api, Clip* clip)
{
	QJsonObject o;
	o["id"] = api->handleFor(clip);
	o["kind"] = "clip";
	o["type"] = clipTypeName(clip);
	o["track"] = api->handleFor(clip->getTrack());
	o["name"] = clip->name();
	o["start"] = toBeats(clip->startPosition());
	o["length"] = toBeats(clip->length());
	o["end"] = toBeats(clip->endPosition());
	o["muted"] = clip->isMuted();
	o["color"] = colorJson(clip->color());
	if (auto mc = dynamic_cast<MidiClip*>(clip))
	{
		o["note_count"] = static_cast<int>(mc->notes().size());
		o["auto_resize"] = mc->getAutoResize();
		if (clip->getTrack()->trackContainer() == Engine::patternStore())
		{
			o["pattern_index"] = clip->getTrack()->getClipNum(clip);
		}
	}
	if (auto sc = dynamic_cast<SampleClip*>(clip))
	{
		o["file"] = sc->sampleFile();
		o["gain"] = sc->gain();
		o["reversed"] = sc->sample().reversed();
		o["fade_in"] = toBeats(sc->fadeIn());
		o["fade_out"] = toBeats(sc->fadeOut());
		o["sample_length"] = toBeats(sc->sampleLength());
	}
	if (auto ac = dynamic_cast<AutomationClip*>(clip))
	{
		static const char* progressions[] = {"discrete", "linear", "cubic"};
		o["progression"] = progressions[static_cast<int>(ac->progressionType())];
		QJsonArray targets;
		for (const auto& obj : ac->objects()) { if (obj) { targets.append(api->handleFor(obj)); } }
		o["targets"] = targets;
		o["point_count"] = ac->getTimeMap().size();
	}
	return o;
}

QJsonObject describeNote(const Note* n)
{
	QJsonObject o;
	o["pitch"] = n->key();
	o["start"] = toBeats(n->pos());
	o["length"] = toBeats(n->length());
	o["velocity"] = n->getVolume();
	o["pan"] = n->getPanning();
	return o;
}

//! Note from {"pitch", "start", "length", "velocity", "pan"} or [pitch, start, length, velocity?, pan?]
Note parseNote(const QJsonValue& v)
{
	QJsonValue pitch, start, length, velocity, pan;
	if (v.isArray())
	{
		const auto a = v.toArray();
		if (a.size() < 3) { throw ScriptError("note arrays need at least [pitch, start, length]"); }
		pitch = a[0]; start = a[1]; length = a[2];
		if (a.size() > 3) { velocity = a[3]; }
		if (a.size() > 4) { pan = a[4]; }
	}
	else if (v.isObject())
	{
		const auto o = v.toObject();
		pitch = o.contains("pitch") ? o.value("pitch") : o.value("key");
		start = o.contains("start") ? o.value("start") : o.value("pos");
		length = o.contains("length") ? o.value("length") : o.value("len");
		velocity = o.contains("velocity") ? o.value("velocity") : o.value("volume");
		pan = o.value("pan");
	}
	else { throw ScriptError("a note must be an object or an array"); }

	if (pitch.isUndefined() || pitch.isNull()) { throw ScriptError("note is missing \"pitch\""); }
	const int key = parseKey(pitch);
	const int pos = toTicks(start.toDouble(0.));
	const int len = std::max(1, toTicks(length.toDouble(1.)));
	const auto vol = static_cast<volume_t>(std::clamp(velocity.toDouble(DefaultVolume), 0., double(MaxVolume)));
	const auto pn = static_cast<panning_t>(std::clamp(pan.toDouble(0.), double(PanningLeft), double(PanningRight)));
	return Note(TimePos(len), TimePos(std::max(0, pos)), key, vol, pn);
}

//! Delete a track the way the editors do: its view goes first, so no widget outlives its model
void deleteTrack(Track* track)
{
	if (auto g = gui())
	{
		gui::TrackContainerView* view = nullptr;
		if (track->trackContainer() == Engine::getSong() && g->songEditor()) { view = g->songEditor()->m_editor; }
		if (track->trackContainer() == Engine::patternStore() && g->patternEditor()) { view = g->patternEditor()->m_editor; }
		if (view)
		{
			for (auto trackView : view->trackViews())
			{
				if (trackView->getTrack() == track)
				{
					view->deleteTrackView(trackView);
					return;
				}
			}
		}
	}
	auto guard = Engine::audioEngine()->requestChangesGuard();
	delete track;
}

void deleteClip(Clip* clip)
{
	auto guard = Engine::audioEngine()->requestChangesGuard();
	delete clip;
}

QPixmap grabWindow(const QString& which)
{
	auto g = gui();
	if (!g) { throw ScriptError("screenshots need the GUI (LMMS was started headless)"); }
	QWidget* w = nullptr;
	if (which.isEmpty() || which == "main") { w = g->mainWindow(); }
	else if (which == "song" || which == "song_editor") { w = static_cast<QWidget*>(g->songEditor()); }
	else if (which == "piano_roll") { w = static_cast<QWidget*>(g->pianoRoll()); }
	else if (which == "pattern" || which == "pattern_editor") { w = static_cast<QWidget*>(g->patternEditor()); }
	else if (which == "automation" || which == "automation_editor") { w = static_cast<QWidget*>(g->automationEditor()); }
	else if (which == "mixer") { w = static_cast<QWidget*>(g->mixerView()); }
	else if (which == "script_console") { w = static_cast<QWidget*>(g->mainWindow()->scriptConsole()); }
	else { throw ScriptError("unknown window (main, song_editor, piano_roll, pattern_editor, automation_editor, mixer, script_console)"); }
	if (!w) { throw ScriptError("that window does not exist"); }
	// editors live in sub-windows: grab the frame including the title bar
	if (w != g->mainWindow() && w->parentWidget()) { w = w->parentWidget(); }
	return w->grab();
}

QWidget* editorWindow(const QString& which)
{
	auto g = gui();
	if (!g) { throw ScriptError("windows need the GUI (LMMS was started headless)"); }
	if (which == "song" || which == "song_editor") { return static_cast<QWidget*>(g->songEditor()); }
	if (which == "piano_roll") { return static_cast<QWidget*>(g->pianoRoll()); }
	if (which == "pattern" || which == "pattern_editor") { return static_cast<QWidget*>(g->patternEditor()); }
	if (which == "automation" || which == "automation_editor") { return static_cast<QWidget*>(g->automationEditor()); }
	if (which == "mixer") { return static_cast<QWidget*>(g->mixerView()); }
	if (which == "script_console") { return static_cast<QWidget*>(g->mainWindow()->scriptConsole()); }
	throw ScriptError("unknown window (song_editor, piano_roll, pattern_editor, automation_editor, mixer, script_console)");
}

void showEditor(QWidget* w)
{
	if (!w) { return; }
	QWidget* frame = w->parentWidget() ? w->parentWidget() : w;
	frame->show();
	frame->raise();
	w->show();
	w->setFocus();
}

//! Peak/RMS statistics for an audio file
QJsonObject analyzeFile(const QString& path)
{
	auto buffer = SampleBuffer::fromFile(path);
	if (!buffer || buffer->size() == 0) { throw ScriptError(QString("could not read audio from %1").arg(path)); }
	const auto& data = buffer->data();
	float peak = 0.f;
	double sumSquares = 0.;
	std::size_t clipped = 0;
	for (std::size_t i = 0; i < buffer->size(); ++i)
	{
		for (int ch = 0; ch < 2; ++ch)
		{
			const float s = data[i][ch];
			peak = std::max(peak, std::abs(s));
			sumSquares += double(s) * s;
			if (std::abs(s) >= 1.f) { ++clipped; }
		}
	}
	const double rms = std::sqrt(sumSquares / (2. * buffer->size()));
	auto db = [](double x) { return x > 1e-9 ? 20. * std::log10(x) : -180.; };
	QJsonObject o;
	o["path"] = path;
	o["seconds"] = double(buffer->size()) / buffer->sampleRate();
	o["sample_rate"] = static_cast<int>(buffer->sampleRate());
	o["peak"] = peak;
	o["peak_db"] = db(peak);
	o["rms_db"] = db(rms);
	o["clipped_samples"] = static_cast<double>(clipped);
	o["silent"] = peak < 1e-4f;
	return o;
}

} // namespace




ScriptApi* ScriptApi::instance()
{
	static ScriptApi* api = new ScriptApi;
	return api;
}




ScriptApi::ScriptApi()
{
	registerSongMethods();
	registerTrackMethods();
	registerClipMethods();
	registerPluginMethods();
	registerMixerMethods();
	registerMiscMethods();
}




int ScriptApi::handleFor(QObject* object)
{
	if (!object) { return 0; }
	auto it = m_handles.find(object);
	if (it != m_handles.end() && m_objects.value(it.value()) == object) { return it.value(); }
	const int handle = m_nextHandle++;
	m_handles[object] = handle;
	m_objects[handle] = object;
	return handle;
}




QObject* ScriptApi::objectFor(int handle) const
{
	return m_objects.value(handle).data();
}




void ScriptApi::add(const QString& name, const QString& help, SyncHandler handler)
{
	m_methods[name] = Method{help, std::move(handler), nullptr};
}




void ScriptApi::addAsync(const QString& name, const QString& help, AsyncHandler handler)
{
	m_methods[name] = Method{help, nullptr, std::move(handler)};
}




QJsonObject ScriptApi::describe() const
{
	QJsonObject out;
	for (const auto& [name, method] : m_methods) { out[name] = method.help; }
	return out;
}




void ScriptApi::call(const QString& method, const QJsonObject& params, const Reply& reply)
{
	const auto it = m_methods.find(method);
	if (it == m_methods.end())
	{
		reply(QJsonValue(), QString("unknown method \"%1\" (call \"api.describe\" for the list)").arg(method));
		return;
	}
	try
	{
		if (it->second.async) { it->second.async(params, reply); }
		else { reply(it->second.sync(params), QString()); }
	}
	catch (const std::exception& e)
	{
		reply(QJsonValue(), QString::fromStdString(e.what()));
	}
}




namespace
{

template<class T>
T* object(ScriptApi* api, const QJsonObject& p, const char* key, const char* what)
{
	const int handle = static_cast<int>(num(p, key));
	QObject* o = api->objectFor(handle);
	if (!o) { throw ScriptError(QString("%1 %2 no longer exists").arg(what).arg(handle)); }
	auto t = dynamic_cast<T*>(o);
	if (!t) { throw ScriptError(QString("object %1 is not a %2").arg(handle).arg(what)); }
	return t;
}

} // namespace




void ScriptApi::registerSongMethods()
{
	add("song.info", "Tempo, time signature, length, playback state and file of the current project", [this](const QJsonObject&) {
		auto song = Engine::getSong();
		QJsonObject o;
		o["tempo"] = song->getTempo();
		o["time_signature"] = QJsonArray{song->getTimeSigModel().getNumerator(), song->getTimeSigModel().getDenominator()};
		o["master_volume"] = song->masterVolume();
		o["master_pitch"] = song->masterPitch();
		o["length_bars"] = song->length();
		o["length_beats"] = toBeats(song->length() * TimePos::ticksPerBar());
		o["beats_per_bar"] = toBeats(TimePos::ticksPerBar());
		o["playing"] = song->isPlaying();
		o["position"] = toBeats(song->getPlayPos(Song::PlayMode::Song).getTicks());
		o["file"] = song->projectFileName();
		o["modified"] = song->isModified();
		const auto& tl = song->getTimeline(Song::PlayMode::Song);
		o["loop"] = QJsonObject{{"enabled", tl.loopEnabled()},
			{"start", toBeats(tl.loopBegin().getTicks())}, {"end", toBeats(tl.loopEnd().getTicks())}};
		o["headless"] = gui() == nullptr;
		return o;
	});

	add("song.set", "Set tempo, time_signature [num, den], master_volume (0-200), master_pitch (semitones)", [this](const QJsonObject& p) {
		auto song = Engine::getSong();
		if (has(p, "tempo")) { song->tempoModel().setValue(static_cast<float>(num(p, "tempo"))); }
		if (has(p, "time_signature"))
		{
			const auto ts = p.value("time_signature").toArray();
			if (ts.size() != 2) { throw ScriptError("time_signature must be [numerator, denominator]"); }
			song->getTimeSigModel().numeratorModel().setValue(ts[0].toInt());
			song->getTimeSigModel().denominatorModel().setValue(ts[1].toInt());
		}
		if (has(p, "master_volume")) { song->setMasterVolume(static_cast<int>(num(p, "master_volume"))); }
		if (has(p, "master_pitch")) { song->setMasterPitch(static_cast<int>(num(p, "master_pitch"))); }
		markModified();
		QJsonValue info;
		call("song.info", {}, [&](const QJsonValue& r, const QString&) { info = r; });
		return info;
	});

	add("song.params", "Automatable song parameters (tempo, master volume/pitch, time signature)", [this](const QJsonObject&) {
		auto song = Engine::getSong();
		QJsonArray out;
		out.append(describeParam(this, &song->tempoModel(), "tempo"));
		for (auto model : song->findChildren<AutomatableModel*>(QString(), Qt::FindDirectChildrenOnly))
		{
			if (model->displayName() == Song::tr("Master volume")) { out.append(describeParam(this, model, "master_volume")); }
			if (model->displayName() == Song::tr("Master pitch")) { out.append(describeParam(this, model, "master_pitch")); }
		}
		out.append(describeParam(this, &song->getTimeSigModel().numeratorModel(), "time_sig_numerator"));
		out.append(describeParam(this, &song->getTimeSigModel().denominatorModel(), "time_sig_denominator"));
		return out;
	});

	add("song.new", "Start a new project: from the default template, from 'template' (a .mpt/.mmp file), "
		"or completely empty with empty=true (no tracks, default tempo)", [](const QJsonObject& p) {
		auto song = Engine::getSong();
		song->stop();
		if (flag(p, "empty", false)) { song->createEmptyProject(); }
		else if (has(p, "template"))
		{
			const QString path = filePath(str(p, "template"));
			if (!QFileInfo::exists(path)) { throw ScriptError(QString("template not found: %1").arg(path)); }
			song->createNewProjectFromTemplate(path);
		}
		else { song->createNewProject(); }
		return QJsonValue(true);
	});

	add("song.open", "Open a project file (.mmp/.mmpz)", [](const QJsonObject& p) {
		const QString path = filePath(str(p, "path"));
		if (!QFileInfo::exists(path)) { throw ScriptError(QString("file not found: %1").arg(path)); }
		Engine::getSong()->stop();
		Engine::getSong()->loadProject(path);
		return QJsonValue(true);
	});

	add("song.save", "Save the project; path is optional when the project already has a file name", [](const QJsonObject& p) {
		auto song = Engine::getSong();
		QString path = has(p, "path") ? filePath(str(p, "path")) : song->projectFileName();
		if (path.isEmpty()) { throw ScriptError("the project has no file name yet: pass a path"); }
		if (!path.endsWith(".mmp") && !path.endsWith(".mmpz")) { path += ".mmpz"; }
		const bool ok = gui() ? song->guiSaveProjectAs(path) : song->saveProjectFile(path);
		if (!ok) { throw ScriptError(QString("could not save %1").arg(path)); }
		return QJsonValue(path);
	});

	add("song.import", "Import a MIDI (or Hydrogen) file into the song", [](const QJsonObject& p) {
		const QString path = filePath(str(p, "path"));
		if (!QFileInfo::exists(path)) { throw ScriptError(QString("file not found: %1").arg(path)); }
		ImportFilter::import(path, Engine::getSong());
		markModified();
		return QJsonValue(true);
	});

	add("song.export_midi", "Export the song as a MIDI file", [](const QJsonObject& p) {
		const QString path = filePath(str(p, "path"));
		Engine::getSong()->exportProjectMidi(std::filesystem::path(path.toStdString()));
		return QJsonValue(path);
	});

	addAsync("song.render", "Render the song to an audio file (wav/flac/ogg/mp3) and return loudness statistics. "
		"Options: sample_rate, bit_depth (16/24/32), loop, between_loop_markers, loop_count, stems (render each track into the directory 'path')",
		[](const QJsonObject& p, const Reply& reply) {
		auto song = Engine::getSong();
		if (song->isExporting()) { throw ScriptError("a render is already running"); }
		QString path = filePath(str(p, "path"));
		const bool stems = flag(p, "stems", false);

		auto format = ProjectRenderer::ExportFileFormat::Wave;
		const QString fmt = str(p, "format", stems ? "wav" : QFileInfo(path).suffix()).toLower();
		if (fmt == "flac") { format = ProjectRenderer::ExportFileFormat::Flac; }
		else if (fmt == "ogg") { format = ProjectRenderer::ExportFileFormat::Ogg; }
		else if (fmt == "mp3") { format = ProjectRenderer::ExportFileFormat::MP3; }
		else if (!fmt.isEmpty() && fmt != "wav") { throw ScriptError(QString("unknown format \"%1\" (wav, flac, ogg, mp3)").arg(fmt)); }
		if (!ProjectRenderer::fileEncodeDevices[static_cast<int>(format)].isAvailable())
		{
			throw ScriptError(QString("this LMMS build cannot write %1 files").arg(fmt));
		}
		if (!stems && QFileInfo(path).suffix().isEmpty()) { path += ProjectRenderer::getFileExtensionFromFormat(format); }
		if (stems) { QDir().mkpath(path); }
		else { QDir().mkpath(QFileInfo(path).absolutePath()); }

		auto bitDepth = OutputSettings::BitDepth::Depth16Bit;
		const int depth = static_cast<int>(num(p, "bit_depth", 16));
		if (depth == 24) { bitDepth = OutputSettings::BitDepth::Depth24Bit; }
		else if (depth == 32) { bitDepth = OutputSettings::BitDepth::Depth32Bit; }
		const auto sampleRate = static_cast<sample_rate_t>(num(p, "sample_rate", 44100));
		OutputSettings os(sampleRate, static_cast<bitrate_t>(num(p, "bitrate", 256)), bitDepth,
			OutputSettings::StereoMode::Stereo);

		song->stop();
		song->setExportLoop(flag(p, "loop", false));
		song->setRenderBetweenMarkers(flag(p, "between_loop_markers", false));
		song->setLoopRenderCount(static_cast<int>(num(p, "loop_count", 1)));

		auto rm = new RenderManager(os, format, path);
		const bool analyze = flag(p, "analyze", true) && !stems;
		QObject::connect(rm, &RenderManager::finished, rm, [rm, reply, path, analyze, stems] {
			rm->deleteLater();
			QJsonObject result;
			if (analyze)
			{
				try { result = analyzeFile(path); }
				catch (const std::exception&) { result["path"] = path; }
			}
			else { result["path"] = path; }
			if (stems)
			{
				QJsonArray files;
				for (const auto& f : QDir(path).entryInfoList(QDir::Files, QDir::Name)) { files.append(f.absoluteFilePath()); }
				result["files"] = files;
			}
			reply(result, QString());
		});
		if (stems) { rm->renderTracks(); }
		else { rm->renderProject(); }
	});

	add("song.play", "Start playback (optionally from a beat position)", [](const QJsonObject& p) {
		auto song = Engine::getSong();
		if (has(p, "from")) { song->setPlayPos(toTicks(num(p, "from")), Song::PlayMode::Song); }
		if (!song->isPlaying() || song->playMode() != Song::PlayMode::Song) { song->playSong(); }
		return QJsonValue(true);
	});

	add("song.stop", "Stop playback", [](const QJsonObject&) {
		Engine::getSong()->stop();
		return QJsonValue(true);
	});

	add("song.pause", "Pause/resume playback", [](const QJsonObject&) {
		Engine::getSong()->togglePause();
		return QJsonValue(true);
	});

	add("song.set_position", "Move the song play head to a beat", [](const QJsonObject& p) {
		Engine::getSong()->setPlayPos(toTicks(num(p, "beat")), Song::PlayMode::Song);
		return QJsonValue(true);
	});

	add("song.set_loop", "Set the song loop: enabled, start, end (beats)", [](const QJsonObject& p) {
		auto& tl = Engine::getSong()->getTimeline(Song::PlayMode::Song);
		if (has(p, "start") || has(p, "end"))
		{
			tl.setLoopPoints(TimePos(toTicks(num(p, "start", toBeats(tl.loopBegin().getTicks())))),
				TimePos(toTicks(num(p, "end", toBeats(tl.loopEnd().getTicks())))));
		}
		if (has(p, "enabled")) { tl.setLoopEnabled(flag(p, "enabled", true)); }
		return QJsonValue(true);
	});

	add("song.tracks", "List tracks of the song (container=\"song\") or the pattern editor (container=\"pattern\")", [this](const QJsonObject& p) {
		QJsonArray out;
		for (auto track : container(str(p, "container", "song"))->tracks()) { out.append(describeTrack(this, track)); }
		return out;
	});

	add("song.add_track", "Add a track. type: instrument|sample|automation|pattern; name; instrument (plugin name); "
		"key (sub-plugin, e.g. a VST3 name); preset (.xpf file); container: song|pattern; color; "
		"params {key: value} for the track, instrument_params {key: value} for the instrument", [this](const QJsonObject& p) {
		const auto type = parseTrackType(str(p, "type", "instrument"));
		TrackContainer* tc = container(str(p, "container", "song"));
		if (tc == Engine::patternStore() && type != Track::Type::Instrument && type != Track::Type::Sample)
		{
			throw ScriptError("the pattern editor can only hold instrument and sample tracks");
		}
		// resolve everything that can fail before the track exists
		QString preset;
		const Plugin::Descriptor* desc = nullptr;
		std::optional<Plugin::Descriptor::SubPluginFeatures::Key> key;
		if (type == Track::Type::Instrument)
		{
			if (has(p, "preset"))
			{
				preset = filePath(str(p, "preset"));
				if (!QFileInfo::exists(preset)) { throw ScriptError(QString("preset not found: %1").arg(preset)); }
			}
			else
			{
				desc = findDescriptor(str(p, "instrument", "tripleoscillator"), Plugin::Type::Instrument);
				if (has(p, "key")) { key = pluginKey(desc, p.value("key")); }
			}
		}
		const auto color = has(p, "color") ? parseColor(p.value("color")) : std::nullopt;

		Track* track = Track::create(type, tc);
		if (!track) { throw ScriptError("could not create the track"); }
		try
		{
			if (auto it = dynamic_cast<InstrumentTrack*>(track))
			{
				if (!preset.isEmpty()) { it->replaceInstrument(DataFile(preset)); }
				else { it->loadInstrument(desc->name, key ? &*key : nullptr); }
				if (has(p, "instrument_params") && it->instrument())
				{
					applyParams(it->instrument(), p.value("instrument_params").toObject());
				}
			}
			if (auto pt = dynamic_cast<PatternTrack*>(track))
			{
				Engine::patternStore()->setCurrentPattern(pt->patternIndex());
			}
			if (has(p, "name")) { track->setName(str(p, "name")); }
			if (color) { track->setColor(color); }
			if (has(p, "params")) { applyParams(track, p.value("params").toObject()); }
		}
		catch (...)
		{
			// let the new track's views finish their queued setup before tearing them down
			QCoreApplication::sendPostedEvents();
			deleteTrack(track);
			throw;
		}
		markModified();
		return describeTrack(this, track);
	});

	add("patterns.info", "Number of patterns in the pattern editor and the current one", [](const QJsonObject&) {
		QJsonObject o;
		o["count"] = Engine::patternStore()->numOfPatterns();
		o["current"] = Engine::patternStore()->currentPattern();
		return o;
	});
}




void ScriptApi::registerTrackMethods()
{
	add("track.info", "Describe a track", [this](const QJsonObject& p) {
		return describeTrack(this, object<Track>(this, p, "id", "track"));
	});

	add("track.set", "Set name, muted, solo, color, volume, panning, pitch, mixer_channel or any params {key: value}", [this](const QJsonObject& p) {
		auto track = object<Track>(this, p, "id", "track");
		if (has(p, "name")) { track->setName(str(p, "name")); }
		if (has(p, "muted")) { track->setMuted(flag(p, "muted", false)); }
		if (has(p, "solo")) { track->setSolo(flag(p, "solo", false)); }
		if (has(p, "color")) { track->setColor(parseColor(p.value("color"))); }
		if (has(p, "volume")) { findParam(track, "vol")->setValue(static_cast<float>(num(p, "volume"))); }
		if (has(p, "panning")) { findParam(track, "pan")->setValue(static_cast<float>(num(p, "panning"))); }
		if (has(p, "pitch")) { findParam(track, "pitch")->setValue(static_cast<float>(num(p, "pitch"))); }
		if (has(p, "mixer_channel"))
		{
			const int ch = static_cast<int>(num(p, "mixer_channel"));
			if (ch < 0 || ch >= Engine::mixer()->numChannels()) { throw ScriptError(QString("mixer channel %1 does not exist").arg(ch)); }
			updateMixerChannelRanges();
			findParam(track, "mixch")->setValue(static_cast<float>(ch));
		}
		if (has(p, "params")) { applyParams(track, p.value("params").toObject()); }
		markModified();
		return describeTrack(this, track);
	});

	add("track.params", "List the track's own parameters (volume, panning, envelopes, arpeggio, ...)", [this](const QJsonObject& p) {
		return describeParams(this, object<Track>(this, p, "id", "track"));
	});

	add("track.delete", "Remove a track", [this](const QJsonObject& p) {
		deleteTrack(object<Track>(this, p, "id", "track"));
		markModified();
		return QJsonValue(true);
	});

	add("track.clone", "Duplicate a track with all its clips", [this](const QJsonObject& p) {
		auto track = object<Track>(this, p, "id", "track");
		Track* copy = track->clone();
		markModified();
		return describeTrack(this, copy);
	});

	add("track.move", "Move a track to another index in its container", [this](const QJsonObject& p) {
		auto track = object<Track>(this, p, "id", "track");
		const int count = static_cast<int>(track->trackContainer()->tracks().size());
		const int index = std::clamp(static_cast<int>(num(p, "index")), 0, count - 1);
		track->trackContainer()->moveTrack(track, index);
		markModified();
		return describeTrack(this, track);
	});

	add("track.clips", "List the clips of a track", [this](const QJsonObject& p) {
		auto track = object<Track>(this, p, "id", "track");
		QJsonArray out;
		for (auto clip : track->getClips()) { out.append(describeClip(this, clip)); }
		return out;
	});

	add("track.add_clip", "Add a clip at start (beats) with optional length/name. Sample tracks: file; "
		"automation tracks: target (param id) and points; instrument tracks: notes", [this](const QJsonObject& p) {
		auto track = object<Track>(this, p, "id", "track");
		if (track->trackContainer() == Engine::patternStore())
		{
			throw ScriptError("tracks in the pattern editor have one clip per pattern: use track.pattern_clip");
		}
		const TimePos start(std::max(0, toTicks(num(p, "start", 0))));
		Clip* clip = track->createClip(start);
		if (!clip) { throw ScriptError("this track type cannot hold clips"); }
		try
		{
			if (has(p, "name")) { clip->setName(str(p, "name")); }
			if (auto sc = dynamic_cast<SampleClip*>(clip); sc && has(p, "file"))
			{
				const QString file = filePath(str(p, "file"));
				if (!QFileInfo::exists(file)) { throw ScriptError(QString("sample not found: %1").arg(file)); }
				sc->setSampleFile(file);
			}
			if (auto mc = dynamic_cast<MidiClip*>(clip); mc && has(p, "notes"))
			{
				for (const auto& n : p.value("notes").toArray()) { mc->addNote(parseNote(n), false); }
			}
			if (auto ac = dynamic_cast<AutomationClip*>(clip))
			{
				if (has(p, "target")) { ac->addObject(object<AutomatableModel>(this, p, "target", "parameter")); }
				if (has(p, "points"))
				{
					for (const auto& pt : p.value("points").toArray())
					{
						const auto a = pt.toArray();
						if (a.size() < 2) { throw ScriptError("automation points are [beat, value]"); }
						ac->putValue(TimePos(toTicks(a[0].toDouble())), static_cast<float>(a[1].toDouble()), false);
					}
				}
			}
			if (has(p, "length"))
			{
				if (auto mc = dynamic_cast<MidiClip*>(clip)) { mc->setAutoResize(false); }
				clip->changeLength(TimePos(std::max(1, toTicks(num(p, "length")))));
			}
			if (has(p, "color")) { clip->setColor(parseColor(p.value("color"))); }
		}
		catch (...)
		{
			QCoreApplication::sendPostedEvents();
			deleteClip(clip);
			throw;
		}
		markModified();
		return describeClip(this, clip);
	});

	add("track.pattern_clip", "The clip of a pattern-editor track for pattern index (creates missing patterns' clips)", [this](const QJsonObject& p) {
		auto track = object<Track>(this, p, "id", "track");
		if (track->trackContainer() != Engine::patternStore()) { throw ScriptError("track is not in the pattern editor"); }
		const int pattern = static_cast<int>(num(p, "pattern", 0));
		if (pattern < 0 || pattern >= std::max(1, Engine::patternStore()->numOfPatterns()))
		{
			throw ScriptError(QString("pattern %1 does not exist: add a pattern track first").arg(pattern));
		}
		return describeClip(this, track->getClip(pattern));
	});

	add("track.instrument", "The instrument plugin of an instrument track", [this](const QJsonObject& p) {
		auto track = object<InstrumentTrack>(this, p, "id", "instrument track");
		if (!track->instrument()) { return QJsonValue(); }
		return QJsonValue(describePlugin(this, track->instrument()));
	});

	add("track.set_instrument", "Replace the instrument: plugin (name), key (sub-plugin), params {key: value}", [this](const QJsonObject& p) {
		auto track = object<InstrumentTrack>(this, p, "id", "instrument track");
		const auto desc = findDescriptor(str(p, "plugin"), Plugin::Type::Instrument);
		std::optional<Plugin::Descriptor::SubPluginFeatures::Key> key;
		if (has(p, "key")) { key = pluginKey(desc, p.value("key")); }
		const QString oldName = track->name();
		Instrument* instrument = track->loadInstrument(desc->name, key ? &*key : nullptr);
		if (has(p, "keep_name") && flag(p, "keep_name", false)) { track->setName(oldName); }
		if (!instrument || !instrument->descriptor() || QString(instrument->descriptor()->name) != desc->name)
		{
			throw ScriptError(QString("could not load instrument \"%1\"").arg(desc->name));
		}
		if (has(p, "params")) { applyParams(instrument, p.value("params").toObject()); }
		markModified();
		return describePlugin(this, instrument);
	});

	add("track.load_preset", "Load an instrument preset file (.xpf) into an instrument track", [this](const QJsonObject& p) {
		auto track = object<InstrumentTrack>(this, p, "id", "instrument track");
		const QString path = filePath(str(p, "path"));
		if (!QFileInfo::exists(path)) { throw ScriptError(QString("preset not found: %1").arg(path)); }
		track->replaceInstrument(DataFile(path));
		return describeTrack(this, track);
	});

	add("track.save_preset", "Save an instrument track (instrument + track settings) as a preset file (.xpf)", [this](const QJsonObject& p) {
		auto track = object<InstrumentTrack>(this, p, "id", "instrument track");
		QString path = filePath(str(p, "path"));
		if (!path.endsWith(".xpf")) { path += ".xpf"; }
		DataFile dataFile(DataFile::Type::InstrumentTrackSettings);
		QDomElement& content(dataFile.content());
		track->savePreset(dataFile, content);
		content.setAttribute("muted", 0);
		content.setAttribute("solo", 0);
		content.setAttribute("mutedBeforeSolo", 0);
		if (!dataFile.writeFile(path)) { throw ScriptError(QString("could not write %1").arg(path)); }
		return QJsonValue(path);
	});

	add("track.effects", "Effects in the track's effect chain", [this](const QJsonObject& p) {
		return describeChain(this, effectChainOf(object<Track>(this, p, "id", "track")));
	});

	add("track.add_effect", "Append an effect to the track's chain: plugin (name), key (sub-plugin), params {key: value}", [this](const QJsonObject& p) {
		auto chain = effectChainOf(object<Track>(this, p, "id", "track"));
		if (!chain) { throw ScriptError("this track type has no effect chain"); }
		return describePlugin(this, addEffect(chain, p));
	});

	add("track.note_on", "Play a note live on an instrument track (pitch, velocity 0-127); pair with track.note_off", [this](const QJsonObject& p) {
		auto track = object<InstrumentTrack>(this, p, "id", "instrument track");
		const int key = parseKey(p.value("pitch"));
		const int vel = static_cast<int>(num(p, "velocity", 100));
		track->processInEvent(MidiEvent(MidiNoteOn, 0, key, std::clamp(vel, 1, 127)));
		return QJsonValue(true);
	});

	add("track.note_off", "Release a note started with track.note_on", [this](const QJsonObject& p) {
		auto track = object<InstrumentTrack>(this, p, "id", "instrument track");
		track->processInEvent(MidiEvent(MidiNoteOff, 0, parseKey(p.value("pitch")), 0));
		return QJsonValue(true);
	});
}




void ScriptApi::registerClipMethods()
{
	add("clip.info", "Describe a clip", [this](const QJsonObject& p) {
		return describeClip(this, object<Clip>(this, p, "id", "clip"));
	});

	add("clip.set", "Set name, start, length, muted, color; sample clips: gain, reversed, fade_in, fade_out, file", [this](const QJsonObject& p) {
		auto clip = object<Clip>(this, p, "id", "clip");
		if (has(p, "name")) { clip->setName(str(p, "name")); }
		if (has(p, "start")) { clip->movePosition(TimePos(std::max(0, toTicks(num(p, "start"))))); }
		if (has(p, "length"))
		{
			if (auto mc = dynamic_cast<MidiClip*>(clip)) { mc->setAutoResize(false); }
			clip->changeLength(TimePos(std::max(1, toTicks(num(p, "length")))));
		}
		if (has(p, "muted") && clip->isMuted() != flag(p, "muted", false)) { clip->toggleMute(); }
		if (has(p, "color")) { clip->setColor(parseColor(p.value("color"))); }
		if (auto sc = dynamic_cast<SampleClip*>(clip))
		{
			if (has(p, "file"))
			{
				const QString file = filePath(str(p, "file"));
				if (!QFileInfo::exists(file)) { throw ScriptError(QString("sample not found: %1").arg(file)); }
				sc->setSampleFile(file);
			}
			if (has(p, "gain")) { sc->setGain(static_cast<float>(num(p, "gain"))); }
			if (has(p, "fade_in")) { sc->setFadeIn(TimePos(toTicks(num(p, "fade_in")))); }
			if (has(p, "fade_out")) { sc->setFadeOut(TimePos(toTicks(num(p, "fade_out")))); }
			if (has(p, "reversed") && sc->sample().reversed() != flag(p, "reversed", false))
			{
				sc->sample().setReversed(flag(p, "reversed", false));
				emit sc->wasReversed();
			}
		}
		markModified();
		return describeClip(this, clip);
	});

	add("clip.delete", "Remove a clip", [this](const QJsonObject& p) {
		deleteClip(object<Clip>(this, p, "id", "clip"));
		markModified();
		return QJsonValue(true);
	});

	add("clip.copy", "Copy a clip to another start position (beats), optionally onto another track of the same type", [this](const QJsonObject& p) {
		auto clip = object<Clip>(this, p, "id", "clip");
		Track* target = has(p, "track") ? object<Track>(this, p, "track", "track") : clip->getTrack();
		if (target->type() != clip->getTrack()->type()) { throw ScriptError("clips can only be copied to tracks of the same type"); }
		Clip* copy = target->createClip(TimePos(std::max(0, toTicks(num(p, "start")))));
		const TimePos pos = copy->startPosition();
		// like Clip::copyStateTo, which also works without a GUI
		QDomDocument doc;
		QDomElement parent = doc.createElement("StateCopy");
		saveObject(clip, doc, parent);
		copy->restoreState(parent.firstChild().toElement());
		copy->movePosition(pos);
		AutomationClip::resolveAllIDs();
		markModified();
		return describeClip(this, copy);
	});

	add("clip.notes", "Notes of a MIDI clip: [{pitch, start, length, velocity, pan}] (start relative to the clip)", [this](const QJsonObject& p) {
		auto clip = object<MidiClip>(this, p, "id", "MIDI clip");
		QJsonArray out;
		for (const auto note : clip->notes()) { out.append(describeNote(note)); }
		return out;
	});

	add("clip.add_notes", "Add notes to a MIDI clip: notes [{pitch, start, length, velocity (0-200, 100 = normal), pan (-100..100)}] "
		"or [[pitch, start, length, velocity?, pan?], ...]; pitch may be a name like \"C4\"", [this](const QJsonObject& p) {
		auto clip = object<MidiClip>(this, p, "id", "MIDI clip");
		std::vector<Note> notes;
		for (const auto& n : p.value("notes").toArray()) { notes.push_back(parseNote(n)); }
		for (const auto& n : notes) { clip->addNote(n, false); }
		clip->rearrangeAllNotes();
		markModified();
		return describeClip(this, clip);
	});

	add("clip.set_notes", "Replace all notes of a MIDI clip (same format as clip.add_notes)", [this](const QJsonObject& p) {
		auto clip = object<MidiClip>(this, p, "id", "MIDI clip");
		std::vector<Note> notes;
		for (const auto& n : p.value("notes").toArray()) { notes.push_back(parseNote(n)); }
		clip->clearNotes();
		for (const auto& n : notes) { clip->addNote(n, false); }
		clip->rearrangeAllNotes();
		markModified();
		return describeClip(this, clip);
	});

	add("clip.clear_notes", "Remove all notes from a MIDI clip", [this](const QJsonObject& p) {
		auto clip = object<MidiClip>(this, p, "id", "MIDI clip");
		clip->clearNotes();
		markModified();
		return describeClip(this, clip);
	});

	add("clip.remove_notes", "Remove notes by index (as returned by clip.notes)", [this](const QJsonObject& p) {
		auto clip = object<MidiClip>(this, p, "id", "MIDI clip");
		std::vector<int> indices;
		for (const auto& v : p.value("indices").toArray()) { indices.push_back(v.toInt()); }
		std::sort(indices.rbegin(), indices.rend());
		indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
		for (int i : indices)
		{
			if (i >= 0 && i < static_cast<int>(clip->notes().size())) { clip->removeNote(clip->notes()[i]); }
		}
		markModified();
		return describeClip(this, clip);
	});

	add("clip.set_steps", "Pattern-editor clips: enable steps (16th notes by default) given as a list of step indices; "
		"optional pitch, velocity, steps (total step count)", [this](const QJsonObject& p) {
		auto clip = object<MidiClip>(this, p, "id", "MIDI clip");
		const int stepTicks = DefaultTicksPerBar / 16;
		const int key = has(p, "pitch") ? parseKey(p.value("pitch")) : DefaultKey;
		const auto vol = static_cast<volume_t>(num(p, "velocity", DefaultVolume));
		std::vector<int> steps;
		int maxStep = has(p, "steps") ? static_cast<int>(num(p, "steps")) - 1 : 15;
		for (const auto& v : p.value("on").toArray())
		{
			if (v.toInt(-1) < 0) { throw ScriptError("steps must be indices >= 0"); }
			steps.push_back(v.toInt());
			maxStep = std::max(maxStep, v.toInt());
		}
		clip->clearNotes();
		const int bars = maxStep / 16 + 1;
		while (clip->length().getTicks() < bars * DefaultTicksPerBar) { clip->addSteps(); }
		for (int s : steps)
		{
			Note n(TimePos(stepTicks), TimePos(s * stepTicks), key, vol);
			n.setType(Note::Type::Step);
			clip->addNote(n, false);
		}
		clip->rearrangeAllNotes();
		markModified();
		return describeClip(this, clip);
	});

	add("clip.automation", "Points of an automation clip: {progression, points: [[beat, value], ...], targets}", [this](const QJsonObject& p) {
		auto clip = object<AutomationClip>(this, p, "id", "automation clip");
		auto o = describeClip(this, clip);
		QJsonArray points;
		for (auto it = clip->getTimeMap().begin(); it != clip->getTimeMap().end(); ++it)
		{
			points.append(QJsonArray{toBeats(it.key()), it.value().getInValue()});
		}
		o["points"] = points;
		return o;
	});

	add("clip.set_automation", "Edit an automation clip: points [[beat, value], ...] (replace=true clears first), "
		"progression (discrete|linear|cubic), tension, target (param id to add)", [this](const QJsonObject& p) {
		auto clip = object<AutomationClip>(this, p, "id", "automation clip");
		if (has(p, "target")) { clip->addObject(object<AutomatableModel>(this, p, "target", "parameter")); }
		if (has(p, "progression"))
		{
			const QString prog = str(p, "progression");
			if (prog == "discrete") { clip->setProgressionType(AutomationClip::ProgressionType::Discrete); }
			else if (prog == "linear") { clip->setProgressionType(AutomationClip::ProgressionType::Linear); }
			else if (prog == "cubic") { clip->setProgressionType(AutomationClip::ProgressionType::CubicHermite); }
			else { throw ScriptError("progression must be discrete, linear or cubic"); }
		}
		if (has(p, "tension")) { clip->setTension(QString::number(num(p, "tension"))); }
		if (has(p, "points"))
		{
			if (flag(p, "replace", true))
			{
				const auto keys = clip->getTimeMap().keys();
				for (int t : keys) { clip->removeNode(TimePos(t)); }
			}
			for (const auto& pt : p.value("points").toArray())
			{
				const auto a = pt.toArray();
				if (a.size() < 2) { throw ScriptError("automation points are [beat, value]"); }
				clip->putValue(TimePos(toTicks(a[0].toDouble())), static_cast<float>(a[1].toDouble()), false);
			}
		}
		markModified();
		return describeClip(this, clip);
	});

	add("clip.open", "Open a clip in its editor (piano roll / automation editor); GUI only", [this](const QJsonObject& p) {
		auto clip = object<Clip>(this, p, "id", "clip");
		if (!gui()) { throw ScriptError("windows need the GUI (LMMS was started headless)"); }
		if (auto mc = dynamic_cast<MidiClip*>(clip))
		{
			gui()->pianoRoll()->setCurrentMidiClip(mc);
			showEditor(static_cast<QWidget*>(gui()->pianoRoll()));
		}
		else if (auto ac = dynamic_cast<AutomationClip*>(clip))
		{
			gui()->automationEditor()->setCurrentClip(ac);
			showEditor(static_cast<QWidget*>(gui()->automationEditor()));
		}
		else { throw ScriptError("only MIDI and automation clips have an editor"); }
		return QJsonValue(true);
	});
}




void ScriptApi::registerPluginMethods()
{
	add("plugins.list", "Installed plugins: type (instrument|effect), sub_plugins=true also lists LADSPA/LV2/VST3 sub-plugins", [](const QJsonObject& p) {
		const QString typeName = str(p, "type", "");
		const bool withKeys = flag(p, "sub_plugins", false);
		QJsonArray out;
		for (auto type : {Plugin::Type::Instrument, Plugin::Type::Effect})
		{
			const QString name = type == Plugin::Type::Instrument ? "instrument" : "effect";
			if (!typeName.isEmpty() && typeName != name) { continue; }
			for (const auto desc : getPluginFactory()->descriptors(type))
			{
				QJsonObject o;
				o["name"] = desc->name;
				o["display_name"] = desc->displayName;
				o["description"] = QCoreApplication::translate("PluginBrowser", desc->description);
				o["type"] = name;
				o["has_sub_plugins"] = desc->subPluginFeatures != nullptr;
				if (withKeys && desc->subPluginFeatures)
				{
					QJsonArray keys;
					for (const auto& k : subPluginKeys(desc)) { keys.append(describeKey(k)); }
					o["sub_plugins"] = keys;
				}
				out.append(o);
			}
		}
		return out;
	});

	add("plugin.info", "Describe an instrument or effect", [this](const QJsonObject& p) {
		return describePlugin(this, object<Plugin>(this, p, "id", "plugin"));
	});

	add("plugin.params", "List parameters of an instrument or effect (key, name, value, range, options)", [this](const QJsonObject& p) {
		return describeParams(this, object<Plugin>(this, p, "id", "plugin"));
	});

	add("plugin.set_params", "Set parameters of an instrument or effect: values {key or display name: value}", [this](const QJsonObject& p) {
		auto plugin = object<Plugin>(this, p, "id", "plugin");
		const auto values = p.value("values").toObject();
		applyParams(plugin, values);
		markModified();
		QJsonObject out;
		for (auto it = values.begin(); it != values.end(); ++it) { out[it.key()] = paramValue(findParam(plugin, it.key())); }
		return out;
	});

	add("plugin.get_state", "The plugin's full settings as XML (the same data a project stores)", [this](const QJsonObject& p) {
		auto plugin = object<Plugin>(this, p, "id", "plugin");
		QDomDocument doc;
		QDomElement root = doc.createElement("state");
		doc.appendChild(root);
		const QDomElement element = saveObject(plugin, doc, root);
		QDomDocument out;
		out.appendChild(out.importNode(element, true));
		return QJsonValue(out.toString(1));
	});

	add("plugin.set_state", "Restore settings from XML produced by plugin.get_state", [this](const QJsonObject& p) {
		auto plugin = object<Plugin>(this, p, "id", "plugin");
		QDomDocument doc;
		if (!doc.setContent(str(p, "xml"))) { throw ScriptError("invalid XML"); }
		plugin->restoreState(doc.documentElement());
		markModified();
		return QJsonValue(true);
	});

	add("plugin.load_file", "Load a file into a plugin (e.g. a sample into AudioFileProcessor, a SoundFont into Sf2Player); "
		"keep_name=false lets the plugin rename its track after the file", [this](const QJsonObject& p) {
		auto plugin = object<Plugin>(this, p, "id", "plugin");
		const QString path = filePath(str(p, "path"));
		if (!QFileInfo::exists(path)) { throw ScriptError(QString("file not found: %1").arg(path)); }
		auto instrument = dynamic_cast<Instrument*>(plugin);
		const QString trackName = instrument ? instrument->instrumentTrack()->name() : QString();
		plugin->loadFile(path);
		if (instrument && flag(p, "keep_name", true)) { instrument->instrumentTrack()->setName(trackName); }
		markModified();
		return QJsonValue(true);
	});

	add("effect.set", "Set enabled, wet (0-1) and/or params {key: value} of an effect", [this](const QJsonObject& p) {
		auto effect = object<Effect>(this, p, "id", "effect");
		if (has(p, "enabled")) { findParam(effect, "on")->setValue(flag(p, "enabled", true) ? 1.f : 0.f); }
		if (has(p, "wet")) { findParam(effect, "wet")->setValue(static_cast<float>(num(p, "wet"))); }
		if (has(p, "params")) { applyParams(effect, p.value("params").toObject()); }
		markModified();
		return describePlugin(this, effect);
	});

	add("effect.delete", "Remove an effect from its chain", [this](const QJsonObject& p) {
		auto effect = object<Effect>(this, p, "id", "effect");
		auto chain = effect->effectChain();
		if (chain) { chain->removeEffect(effect); }
		effect->deleteLater();
		markModified();
		return QJsonValue(true);
	});

	add("effect.move", "Move an effect up (-1) or down (+1) in its chain", [this](const QJsonObject& p) {
		auto effect = object<Effect>(this, p, "id", "effect");
		auto chain = effect->effectChain();
		if (!chain) { throw ScriptError("the effect is not in an effect chain"); }
		const int steps = static_cast<int>(num(p, "by", 1));
		for (int i = 0; i < std::abs(steps); ++i)
		{
			if (steps < 0) { chain->moveUp(effect); }
			else { chain->moveDown(effect); }
		}
		markModified();
		return describeChain(this, chain);
	});

	add("param.get", "Describe a parameter by handle", [this](const QJsonObject& p) {
		auto model = object<AutomatableModel>(this, p, "id", "parameter");
		return describeParam(this, model, model->property("_lmmsSaveName").toString());
	});

	add("param.set", "Set a parameter by handle (number, bool, or option text for choice parameters)", [this](const QJsonObject& p) {
		auto model = object<AutomatableModel>(this, p, "id", "parameter");
		setParamValue(model, p.value("value"));
		markModified();
		return paramValue(model);
	});

	add("param.find", "Find a parameter of a track or plugin (owner = its id) by key or display name", [this](const QJsonObject& p) {
		QObject* owner = objectFor(static_cast<int>(num(p, "owner")));
		if (!owner) { throw ScriptError("owner no longer exists"); }
		auto model = findParam(owner, str(p, "name"));
		return describeParam(this, model, model->property("_lmmsSaveName").toString());
	});
}




void ScriptApi::registerMixerMethods()
{
	auto describeChannel = [this](int i) {
		auto ch = Engine::mixer()->mixerChannel(i);
		QJsonObject o;
		o["index"] = i;
		o["name"] = i == 0 ? QString("Master") : ch->m_name;
		o["volume"] = ch->m_volumeModel.value();
		o["muted"] = ch->m_muteModel.value();
		o["solo"] = ch->m_soloModel.value();
		o["color"] = colorJson(ch->color());
		QJsonArray sends;
		for (auto route : ch->m_sends)
		{
			sends.append(QJsonObject{{"to", route->receiverIndex()}, {"amount", route->amount()->value()},
				{"param", handleFor(route->amount())}});
		}
		o["sends"] = sends;
		o["effects"] = describeChain(this, &ch->m_fxChain);
		o["params"] = QJsonObject{{"volume", handleFor(&ch->m_volumeModel)}, {"muted", handleFor(&ch->m_muteModel)},
			{"solo", handleFor(&ch->m_soloModel)}};
		return o;
	};

	add("mixer.channels", "List mixer channels (0 = master) with volume, sends and effects", [describeChannel](const QJsonObject&) {
		QJsonArray out;
		for (int i = 0; i < Engine::mixer()->numChannels(); ++i) { out.append(describeChannel(i)); }
		return out;
	});

	add("mixer.channel", "Describe one mixer channel", [describeChannel](const QJsonObject& p) {
		return describeChannel(mixerChannel(p)->index());
	});

	add("mixer.add_channel", "Add a mixer channel (name optional); returns it", [describeChannel](const QJsonObject& p) {
		const int index = gui() && gui()->mixerView() ? gui()->mixerView()->addNewChannel() : Engine::mixer()->createChannel();
		if (has(p, "name")) { Engine::mixer()->mixerChannel(index)->m_name = str(p, "name"); }
		if (gui() && gui()->mixerView()) { gui()->mixerView()->updateMixerChannel(index); }
		updateMixerChannelRanges();
		markModified();
		return describeChannel(index);
	});

	add("mixer.set", "Set name, volume (0-2, 1 = unity), muted, solo, color of a mixer channel", [describeChannel](const QJsonObject& p) {
		auto ch = mixerChannel(p);
		if (has(p, "name")) { ch->m_name = str(p, "name"); }
		if (has(p, "volume")) { ch->m_volumeModel.setValue(static_cast<float>(num(p, "volume"))); }
		if (has(p, "muted")) { ch->m_muteModel.setValue(flag(p, "muted", false)); }
		if (has(p, "solo")) { ch->m_soloModel.setValue(flag(p, "solo", false)); }
		if (has(p, "color")) { ch->setColor(parseColor(p.value("color"))); }
		if (gui() && gui()->mixerView()) { gui()->mixerView()->updateMixerChannel(ch->index()); }
		markModified();
		return describeChannel(ch->index());
	});

	add("mixer.delete_channel", "Delete a mixer channel (tracks routed to it go to master)", [](const QJsonObject& p) {
		const int index = mixerChannel(p)->index();
		if (index == 0) { throw ScriptError("the master channel cannot be deleted"); }
		if (gui() && gui()->mixerView()) { gui()->mixerView()->deleteChannel(index); }
		else
		{
			Engine::mixer()->clearChannel(index);
			Engine::mixer()->deleteChannel(index);
			updateMixerChannelRanges();
		}
		markModified();
		return QJsonValue(true);
	});

	add("mixer.send", "Route channel 'from' to channel 'to' with amount (0-1); amount 0 removes the send", [describeChannel](const QJsonObject& p) {
		const int from = mixerChannel(p, "from")->index();
		const int to = mixerChannel(p, "to")->index();
		const float amount = static_cast<float>(num(p, "amount", 1.0));
		auto mixer = Engine::mixer();
		if (amount <= 0.f) { mixer->deleteChannelSend(from, to); }
		else if (auto model = mixer->channelSendModel(from, to)) { model->setValue(amount); }
		else
		{
			if (mixer->isInfiniteLoop(from, to)) { throw ScriptError("that send would create a feedback loop"); }
			mixer->createChannelSend(from, to, amount);
		}
		if (gui() && gui()->mixerView()) { gui()->mixerView()->updateMixerChannel(from); }
		markModified();
		return describeChannel(from);
	});

	add("mixer.add_effect", "Append an effect to a mixer channel: index, plugin, key, params", [this](const QJsonObject& p) {
		return describePlugin(this, addEffect(&mixerChannel(p)->m_fxChain, p));
	});
}




void ScriptApi::registerMiscMethods()
{
	add("api.describe", "All methods with a one-line description", [this](const QJsonObject&) {
		QJsonObject o;
		o["lmms_version"] = LMMS_VERSION;
		o["api_version"] = ApiVersion;
		o["methods"] = describe();
		return o;
	});

	add("api.ping", "Returns \"pong\"", [](const QJsonObject&) { return QJsonValue("pong"); });

	add("api.batch", "Run several calls at once: calls [{method, params}] -> [{result} or {error}]", [this](const QJsonObject& p) {
		QJsonArray out;
		for (const auto& c : p.value("calls").toArray())
		{
			const auto o = c.toObject();
			const QString method = o.value("method").toString();
			if (method == "api.batch") { throw ScriptError("api.batch cannot be nested"); }
			QJsonObject entry;
			// an asynchronous method would reply after this call has returned, into a dead stack frame
			if (const auto it = m_methods.find(method); it != m_methods.end() && it->second.async)
			{
				entry["error"] = "asynchronous methods cannot be batched";
				out.append(entry);
				continue;
			}
			call(method, o.value("params").toObject(), [&entry](const QJsonValue& r, const QString& e) {
				if (e.isEmpty()) { entry["result"] = r; }
				else { entry["error"] = e; }
			});
			out.append(entry);
		}
		return out;
	});

	add("app.quit", "Quit LMMS (exit code optional)", [this](const QJsonObject& p) {
		const int code = static_cast<int>(num(p, "code", 0));
		QTimer::singleShot(0, this, [this, code] {
			// started with --script/--script-server: main() listens and exits with the code
			if (receivers(SIGNAL(quitRequested(int))) > 0) { emit quitRequested(code); }
			// script console: close the window the normal way, which asks about unsaved changes
			else if (gui()) { gui()->mainWindow()->close(); }
			else { QCoreApplication::exit(code); }
		});
		return QJsonValue(true);
	});

	add("app.message", "Show a message in the Script Console / on stdout", [this](const QJsonObject& p) {
		emit messagePosted(str(p, "text"));
		return QJsonValue(true);
	});

	add("app.paths", "Real directories of factory samples/presets/projects and the user's LMMS folders", [](const QJsonObject&) {
		// "data:/..." are Qt search paths: resolve them to the first directory that exists
		auto resolve = [](const QString& dir) {
			if (!dir.startsWith("data:")) { return QDir(dir).absolutePath(); }
			const QString relative = dir.mid(5);
			for (const auto& base : QDir::searchPaths("data"))
			{
				const QString candidate = QDir(base).filePath(relative.startsWith('/') ? relative.mid(1) : relative);
				if (QFileInfo(candidate).isDir()) { return QDir(candidate).absolutePath(); }
			}
			return QString();
		};
		auto cm = ConfigManager::inst();
		QJsonObject o;
		o["factory_samples"] = resolve(cm->factorySamplesDir());
		o["factory_presets"] = resolve(cm->factoryPresetsDir());
		o["factory_projects"] = resolve(cm->factoryProjectsDir());
		o["user_dir"] = resolve(cm->workingDir());
		o["user_samples"] = resolve(cm->userSamplesDir());
		o["user_presets"] = resolve(cm->userPresetsDir());
		o["user_projects"] = resolve(cm->userProjectsDir());
		return o;
	});

	add("app.analyze", "Peak/RMS statistics of an audio file", [](const QJsonObject& p) {
		return analyzeFile(filePath(str(p, "path")));
	});

	add("gui.screenshot", "Save a PNG of a window: main (default), song_editor, piano_roll, pattern_editor, automation_editor, mixer, script_console", [](const QJsonObject& p) {
		const QString path = filePath(str(p, "path"));
		QDir().mkpath(QFileInfo(path).absolutePath());
		QCoreApplication::processEvents();
		if (!grabWindow(str(p, "window", "main")).save(path)) { throw ScriptError(QString("could not write %1").arg(path)); }
		return QJsonValue(path);
	});

	add("gui.show", "Show a window: song_editor, piano_roll, pattern_editor, automation_editor, mixer, script_console", [](const QJsonObject& p) {
		showEditor(editorWindow(str(p, "window")));
		return QJsonValue(true);
	});
}


} // namespace lmms::scripting
