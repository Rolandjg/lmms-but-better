/*
 * Vst3Plugin.cpp - a loaded instance of a VST3 plugin
 *
 * Copyright (c) 2026 LMMS developers
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

#include "Vst3Plugin.h"

#include <bit>
#include <cstring>
#include <stdexcept>

#include <QDomDocument>
#include <QDomElement>
#include <QTimer>

#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include "AudioEngine.h"
#include "Engine.h"
#include "MidiEvent.h"
#include "SampleFrame.h"
#include "Song.h"
#include "base64.h"

#include "Vst3Basics.h"
#include "Vst3HostApp.h"
#include "Vst3Module.h"

namespace lmms::vst3
{

using namespace Steinberg;

namespace
{

bool iidEqual(const TUID a, const TUID b)
{
	return std::memcmp(a, b, sizeof(TUID)) == 0;
}

//! Maximum number of parameters we create automatable models for
constexpr std::size_t MaxParamModels = 4096;

} // namespace


/*
	IParamValueQueue / IParameterChanges implementations.
	These live for the whole plugin lifetime and are refilled each period,
	so refcounting is a no-op.
*/

class Vst3ParamQueue : public Vst::IParamValueQueue
{
public:
	Vst::ParamID id = 0;
	struct Point
	{
		int32 offset;
		double value;
	};
	std::vector<Point> points;

	Vst3ParamQueue() { points.reserve(8); }
	virtual ~Vst3ParamQueue() = default;

	tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
	{
		if (iidEqual(iid, FUnknown::iid) || iidEqual(iid, Vst::IParamValueQueue::iid))
		{
			*obj = static_cast<Vst::IParamValueQueue*>(this);
			return kResultOk;
		}
		*obj = nullptr;
		return kNoInterface;
	}
	uint32 PLUGIN_API addRef() override { return 1000; }
	uint32 PLUGIN_API release() override { return 1000; }

	Vst::ParamID PLUGIN_API getParameterId() override { return id; }
	int32 PLUGIN_API getPointCount() override { return static_cast<int32>(points.size()); }

	tresult PLUGIN_API getPoint(int32 index, int32& sampleOffset,
		Vst::ParamValue& value) override
	{
		if (index < 0 || index >= static_cast<int32>(points.size())) { return kInvalidArgument; }
		sampleOffset = points[index].offset;
		value = points[index].value;
		return kResultOk;
	}

	tresult PLUGIN_API addPoint(int32 sampleOffset, Vst::ParamValue value,
		int32& index) override
	{
		points.push_back({sampleOffset, value});
		index = static_cast<int32>(points.size()) - 1;
		return kResultOk;
	}
};


class Vst3ParamChanges : public Vst::IParameterChanges
{
public:
	virtual ~Vst3ParamChanges() = default;

	tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
	{
		if (iidEqual(iid, FUnknown::iid) || iidEqual(iid, Vst::IParameterChanges::iid))
		{
			*obj = static_cast<Vst::IParameterChanges*>(this);
			return kResultOk;
		}
		*obj = nullptr;
		return kNoInterface;
	}
	uint32 PLUGIN_API addRef() override { return 1000; }
	uint32 PLUGIN_API release() override { return 1000; }

	int32 PLUGIN_API getParameterCount() override { return m_used; }

	Vst::IParamValueQueue* PLUGIN_API getParameterData(int32 index) override
	{
		if (index < 0 || index >= m_used) { return nullptr; }
		return m_queues[index].get();
	}

	Vst::IParamValueQueue* PLUGIN_API addParameterData(const Vst::ParamID& id,
		int32& index) override
	{
		for (int32 i = 0; i < m_used; ++i)
		{
			if (m_queues[i]->id == id)
			{
				index = i;
				return m_queues[i].get();
			}
		}
		if (m_used == static_cast<int32>(m_queues.size()))
		{
			m_queues.push_back(std::make_unique<Vst3ParamQueue>());
		}
		auto queue = m_queues[m_used].get();
		queue->id = id;
		queue->points.clear();
		index = m_used;
		++m_used;
		return queue;
	}

	void clear() { m_used = 0; }

private:
	std::vector<std::unique_ptr<Vst3ParamQueue>> m_queues;
	int32 m_used = 0;
};


class Vst3EventList : public Vst::IEventList
{
public:
	Vst3EventList() { m_events.reserve(256); }
	virtual ~Vst3EventList() = default;

	tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
	{
		if (iidEqual(iid, FUnknown::iid) || iidEqual(iid, Vst::IEventList::iid))
		{
			*obj = static_cast<Vst::IEventList*>(this);
			return kResultOk;
		}
		*obj = nullptr;
		return kNoInterface;
	}
	uint32 PLUGIN_API addRef() override { return 1000; }
	uint32 PLUGIN_API release() override { return 1000; }

	int32 PLUGIN_API getEventCount() override { return static_cast<int32>(m_events.size()); }

	tresult PLUGIN_API getEvent(int32 index, Vst::Event& e) override
	{
		if (index < 0 || index >= static_cast<int32>(m_events.size())) { return kInvalidArgument; }
		e = m_events[index];
		return kResultOk;
	}

	tresult PLUGIN_API addEvent(Vst::Event& e) override
	{
		m_events.push_back(e);
		return kResultOk;
	}

	void clear() { m_events.clear(); }

private:
	std::vector<Vst::Event> m_events;
};


//! IComponentHandler given to the edit controller; forwards GUI edits and
//! restart requests to the Vst3Plugin
class Vst3ComponentHandler : public Vst::IComponentHandler
{
public:
	explicit Vst3ComponentHandler(Vst3Plugin* plugin) : m_plugin(plugin) {}
	virtual ~Vst3ComponentHandler() = default;

	void invalidate() { m_plugin = nullptr; }

	tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
	{
		if (iidEqual(iid, FUnknown::iid) || iidEqual(iid, Vst::IComponentHandler::iid))
		{
			addRef();
			*obj = static_cast<Vst::IComponentHandler*>(this);
			return kResultOk;
		}
		*obj = nullptr;
		return kNoInterface;
	}
	uint32 PLUGIN_API addRef() override { return ++m_refCount; }
	uint32 PLUGIN_API release() override
	{
		const auto r = --m_refCount;
		if (r == 0) { delete this; return 0; }
		return r;
	}

	tresult PLUGIN_API beginEdit(Vst::ParamID) override { return kResultOk; }

	tresult PLUGIN_API performEdit(Vst::ParamID id, Vst::ParamValue valueNormalized) override
	{
		if (m_plugin) { m_plugin->parameterEditedByGui(id, valueNormalized); }
		return kResultOk;
	}

	tresult PLUGIN_API endEdit(Vst::ParamID) override { return kResultOk; }

	tresult PLUGIN_API restartComponent(int32 flags) override
	{
		if (m_plugin) { m_plugin->componentRestartRequested(flags); }
		return kResultOk;
	}

private:
	Vst3Plugin* m_plugin;
	std::atomic<int32> m_refCount{1};
};




/*
	Vst3Plugin
*/

Vst3Plugin::Vst3Plugin(Model* model, const QString& uid, const QString& fileHint) :
	QObject(model),
	m_model(model)
{
	const Vst3ClassInfo* info = Vst3Manager::instance()->findByUid(uid, fileHint);
	if (!info)
	{
		throw std::runtime_error(
			QString("VST3 plugin with uid %1 not found").arg(uid).toStdString());
	}
	m_classInfo = *info;

	QString error;
	m_module = Vst3Module::open(m_classInfo.modulePath, &error);
	if (!m_module)
	{
		throw std::runtime_error(
			QString("could not load VST3 module: %1").arg(error).toStdString());
	}

	TUID cid;
	tuidFromString(m_classInfo.uid, cid);
	Vst::IComponent* component = nullptr;
	if (m_module->factory()->createInstance(cid, Vst::IComponent::iid,
			reinterpret_cast<void**>(&component)) != kResultOk || !component)
	{
		throw std::runtime_error("could not create VST3 component");
	}
	m_component = owned(component);

	if (m_component->initialize(Vst3HostApp::instance()) != kResultOk)
	{
		throw std::runtime_error("could not initialize VST3 component");
	}
	m_componentInitialized = true;

	// the edit controller may live inside the component ("single component
	// effect") or be a separate class
	Vst::IEditController* controller = nullptr;
	if (m_component->queryInterface(Vst::IEditController::iid,
			reinterpret_cast<void**>(&controller)) == kResultOk && controller)
	{
		m_controller = owned(controller);
		m_singleComponent = true;
	}
	else
	{
		TUID controllerCid = {};
		if (m_component->getControllerClassId(controllerCid) == kResultOk)
		{
			Vst::IEditController* separate = nullptr;
			if (m_module->factory()->createInstance(controllerCid, Vst::IEditController::iid,
					reinterpret_cast<void**>(&separate)) == kResultOk && separate)
			{
				m_controller = owned(separate);
				if (m_controller->initialize(Vst3HostApp::instance()) != kResultOk)
				{
					m_controller = nullptr;
				}
			}
		}
	}

	Vst::IAudioProcessor* processor = nullptr;
	if (m_component->queryInterface(Vst::IAudioProcessor::iid,
			reinterpret_cast<void**>(&processor)) != kResultOk || !processor)
	{
		throw std::runtime_error("VST3 plugin has no audio processor");
	}
	m_processor = owned(processor);

	if (m_controller)
	{
		m_handler = new Vst3ComponentHandler(this);
		m_controller->setComponentHandler(m_handler);

		if (!m_singleComponent)
		{
			Vst::IConnectionPoint* compCP = nullptr;
			Vst::IConnectionPoint* ctrlCP = nullptr;
			m_component->queryInterface(Vst::IConnectionPoint::iid,
				reinterpret_cast<void**>(&compCP));
			m_controller->queryInterface(Vst::IConnectionPoint::iid,
				reinterpret_cast<void**>(&ctrlCP));
			if (compCP && ctrlCP)
			{
				m_componentCP = owned(compCP);
				m_controllerCP = owned(ctrlCP);
				m_componentCP->connect(m_controllerCP);
				m_controllerCP->connect(m_componentCP);
			}
			else
			{
				if (compCP) { compCP->release(); }
				if (ctrlCP) { ctrlCP->release(); }
			}

			// bring the controller in sync with the component
			auto stream = owned(new MemoryStream());
			if (m_component->getState(stream) == kResultOk)
			{
				stream->rewind();
				m_controller->setComponentState(stream);
			}
		}

		Vst::IMidiMapping* mapping = nullptr;
		if (m_controller->queryInterface(Vst::IMidiMapping::iid,
				reinterpret_cast<void**>(&mapping)) == kResultOk && mapping)
		{
			m_midiMapping = owned(mapping);
		}

		initParameters();
	}

	m_events = std::make_unique<Vst3EventList>();
	m_outEvents = std::make_unique<Vst3EventList>();
	m_inChanges = std::make_unique<Vst3ParamChanges>();
	m_outChanges = std::make_unique<Vst3ParamChanges>();
	m_pendingEvents.reserve(64);
	m_pendingCc.reserve(64);

	m_sampleRate = Engine::audioEngine()->outputSampleRate();
	m_blockSize = Engine::audioEngine()->framesPerPeriod();

	setupBuses();
	setupProcessing();
	allocateBuffers();
	activate();

	// plugin GUI edits need to reach the edit controller regularly for
	// correct display; automation runs on the audio thread, so sync here
	m_controllerSyncTimer = new QTimer(this);
	m_controllerSyncTimer->setInterval(50);
	connect(m_controllerSyncTimer, &QTimer::timeout,
		this, &Vst3Plugin::syncControllerFromModels);
	m_controllerSyncTimer->start();
}




Vst3Plugin::~Vst3Plugin()
{
	{
		std::lock_guard<std::mutex> lock{m_processMutex};
		deactivate();
	}

	if (m_componentCP && m_controllerCP)
	{
		m_componentCP->disconnect(m_controllerCP);
		m_controllerCP->disconnect(m_componentCP);
		m_componentCP = nullptr;
		m_controllerCP = nullptr;
	}

	if (m_controller)
	{
		m_controller->setComponentHandler(nullptr);
		if (!m_singleComponent) { m_controller->terminate(); }
	}
	if (m_handler)
	{
		m_handler->invalidate();
		m_handler->release();
		m_handler = nullptr;
	}

	m_midiMapping = nullptr;
	m_processor = nullptr;
	m_controller = nullptr;

	if (m_component && m_componentInitialized) { m_component->terminate(); }
	m_component = nullptr;
	m_module = nullptr;
}




void Vst3Plugin::initParameters()
{
	const int32 count = m_controller->getParameterCount();
	for (int32 i = 0; i < count && m_params.size() < MaxParamModels; ++i)
	{
		Vst::ParameterInfo info = {};
		if (m_controller->getParameterInfo(i, info) != kResultOk) { continue; }
		if (info.flags & Vst::ParameterInfo::kIsHidden) { continue; }
		if (info.flags & Vst::ParameterInfo::kIsReadOnly) { continue; }
		if (info.flags & Vst::ParameterInfo::kIsBypass) { continue; }

		auto param = std::make_unique<Param>();
		param->id = info.id;
		param->title = fromVstString(info.title);
		param->shortTitle = fromVstString(info.shortTitle);
		if (param->shortTitle.isEmpty()) { param->shortTitle = param->title; }
		param->unit = fromVstString(info.units);
		param->defaultNormalized = info.defaultNormalizedValue;

		const float step = info.stepCount > 0 ? 1.f / info.stepCount : 0.00001f;
		const auto value = static_cast<float>(m_controller->getParamNormalized(info.id));
		param->model = new FloatModel(value, 0.f, 1.f, step, m_model, param->title);
		param->pendingValue = value;

		Param* raw = param.get();
		connect(param->model, &FloatModel::dataChanged,
			this, [this, raw]() { onModelChanged(raw); }, Qt::DirectConnection);

		m_paramById[info.id] = raw;
		m_params.push_back(std::move(param));
	}
}




void Vst3Plugin::onModelChanged(Param* param)
{
	param->pendingValue = param->model->value();
	param->dirty = true;
	if (!m_settingFromController) { param->controllerDirty = true; }
}




void Vst3Plugin::syncControllerFromModels()
{
	if (!m_controller) { return; }
	for (const auto& param : m_params)
	{
		if (param->controllerDirty.exchange(false))
		{
			m_controller->setParamNormalized(param->id, param->model->value());
		}
	}
}




void Vst3Plugin::parameterEditedByGui(Vst::ParamID id, double normalized)
{
	const auto it = m_paramById.find(id);
	if (it == m_paramById.end())
	{
		// parameter without model (e.g. more params than MaxParamModels)
		std::lock_guard<std::mutex> lock{m_midiMutex};
		m_pendingCc.emplace_back(id, normalized);
		return;
	}

	Param* param = it->second;
	// route to the processor directly, model quantization must not eat it
	param->pendingValue = normalized;
	param->dirty = true;

	m_settingFromController = true;
	param->model->setValue(static_cast<float>(normalized));
	m_settingFromController = false;
}




void Vst3Plugin::componentRestartRequested(int32 flags)
{
	if (flags & Vst::kParamValuesChanged)
	{
		QMetaObject::invokeMethod(this,
			[this]() { refreshModelsFromController(true); }, Qt::QueuedConnection);
	}
	// kLatencyChanged, kIoChanged etc. are not handled yet
}




void Vst3Plugin::refreshModelsFromController(bool pushToProcessor)
{
	if (!m_controller) { return; }
	for (const auto& param : m_params)
	{
		const auto value = static_cast<float>(m_controller->getParamNormalized(param->id));
		m_settingFromController = true;
		param->model->setValue(value);
		m_settingFromController = false;
		if (!pushToProcessor)
		{
			param->pendingValue = value;
			param->dirty = false;
		}
	}
	emit pluginModelChanged();
}




void Vst3Plugin::setupBuses()
{
	using namespace Vst;

	const auto arrangementFor = [](int32 channels) -> SpeakerArrangement
	{
		switch (channels)
		{
			case 0: return 0;
			case 1: return SpeakerArr::kMono;
			case 2: return SpeakerArr::kStereo;
			default: return (SpeakerArrangement{1} << channels) - 1;
		}
	};

	const int32 numIn = m_component->getBusCount(kAudio, kInput);
	const int32 numOut = m_component->getBusCount(kAudio, kOutput);

	std::vector<SpeakerArrangement> inArr(numIn);
	std::vector<SpeakerArrangement> outArr(numOut);
	std::vector<bool> inMain(numIn), outMain(numOut);

	for (int32 i = 0; i < numIn; ++i)
	{
		BusInfo busInfo = {};
		m_component->getBusInfo(kAudio, kInput, i, busInfo);
		inMain[i] = busInfo.busType == kMain;
		inArr[i] = inMain[i] ? SpeakerArr::kStereo : arrangementFor(busInfo.channelCount);
	}
	for (int32 i = 0; i < numOut; ++i)
	{
		BusInfo busInfo = {};
		m_component->getBusInfo(kAudio, kOutput, i, busInfo);
		outMain[i] = busInfo.busType == kMain;
		outArr[i] = outMain[i] ? SpeakerArr::kStereo : arrangementFor(busInfo.channelCount);
	}

	if (numIn > 0 || numOut > 0)
	{
		m_processor->setBusArrangements(
			numIn > 0 ? inArr.data() : nullptr, numIn,
			numOut > 0 ? outArr.data() : nullptr, numOut);
	}

	// the plugin may have rejected our arrangements - ask what it uses now
	for (int32 i = 0; i < numIn; ++i)
	{
		SpeakerArrangement arr;
		if (m_processor->getBusArrangement(kInput, i, arr) == kResultOk) { inArr[i] = arr; }
	}
	for (int32 i = 0; i < numOut; ++i)
	{
		SpeakerArrangement arr;
		if (m_processor->getBusArrangement(kOutput, i, arr) == kResultOk) { outArr[i] = arr; }
	}

	m_inBuses.clear();
	m_outBuses.clear();
	m_mainIn = -1;
	m_mainOut = -1;

	for (int32 i = 0; i < numIn; ++i)
	{
		AudioBus bus;
		bus.channels = std::popcount(static_cast<uint64>(inArr[i]));
		bus.isMain = inMain[i];
		if (bus.isMain && m_mainIn < 0) { m_mainIn = i; }
		m_inBuses.push_back(std::move(bus));
		m_component->activateBus(kAudio, kInput, i, true);
	}
	if (m_mainIn < 0 && numIn > 0) { m_mainIn = 0; }

	for (int32 i = 0; i < numOut; ++i)
	{
		AudioBus bus;
		bus.channels = std::popcount(static_cast<uint64>(outArr[i]));
		bus.isMain = outMain[i];
		if (bus.isMain && m_mainOut < 0) { m_mainOut = i; }
		m_outBuses.push_back(std::move(bus));
		m_component->activateBus(kAudio, kOutput, i, true);
	}
	if (m_mainOut < 0 && numOut > 0) { m_mainOut = 0; }

	if (m_component->getBusCount(kEvent, kInput) > 0)
	{
		m_component->activateBus(kEvent, kInput, 0, true);
		m_hasEventInput = true;
	}
}




void Vst3Plugin::setupProcessing()
{
	Vst::ProcessSetup setup;
	setup.processMode = Vst::kRealtime;
	setup.symbolicSampleSize = Vst::kSample32;
	setup.maxSamplesPerBlock = m_blockSize;
	setup.sampleRate = m_sampleRate;
	m_processor->setupProcessing(setup);
}




void Vst3Plugin::allocateBuffers()
{
	const auto setup = [this](std::vector<AudioBus>& buses,
		std::vector<Vst::AudioBusBuffers>& busBuffers)
	{
		busBuffers.clear();
		for (auto& bus : buses)
		{
			bus.buffers.assign(bus.channels, std::vector<float>(m_blockSize, 0.f));
			bus.pointers.clear();
			for (auto& channel : bus.buffers) { bus.pointers.push_back(channel.data()); }

			Vst::AudioBusBuffers buffers;
			buffers.numChannels = bus.channels;
			buffers.silenceFlags = 0;
			buffers.channelBuffers32 = bus.pointers.data();
			busBuffers.push_back(buffers);
		}
	};
	setup(m_inBuses, m_inBusBuffers);
	setup(m_outBuses, m_outBusBuffers);
}




void Vst3Plugin::activate()
{
	if (m_processing) { return; }
	m_component->setActive(true);
	m_processor->setProcessing(true);
	m_processing = true;
}




void Vst3Plugin::deactivate()
{
	if (!m_processing) { return; }
	m_processor->setProcessing(false);
	m_component->setActive(false);
	m_processing = false;
}




void Vst3Plugin::reconfigure()
{
	std::lock_guard<std::mutex> lock{m_processMutex};
	deactivate();
	m_sampleRate = Engine::audioEngine()->outputSampleRate();
	m_blockSize = Engine::audioEngine()->framesPerPeriod();
	setupProcessing();
	allocateBuffers();
	activate();
}




void Vst3Plugin::handleMidiInputEvent(const MidiEvent& event, const TimePos&, f_cnt_t offset)
{
	using namespace Vst;

	switch (event.type())
	{
		case MidiEventTypes::MidiNoteOn:
		{
			Event e = {};
			e.busIndex = 0;
			e.sampleOffset = offset;
			e.type = Event::kNoteOnEvent;
			e.noteOn.channel = event.channel();
			e.noteOn.pitch = static_cast<int16>(event.key());
			e.noteOn.tuning = 0.f;
			e.noteOn.velocity = event.velocity() / 127.f;
			e.noteOn.length = 0;
			e.noteOn.noteId = -1;
			std::lock_guard<std::mutex> lock{m_midiMutex};
			m_pendingEvents.push_back(e);
			break;
		}
		case MidiEventTypes::MidiNoteOff:
		{
			Event e = {};
			e.busIndex = 0;
			e.sampleOffset = offset;
			e.type = Event::kNoteOffEvent;
			e.noteOff.channel = event.channel();
			e.noteOff.pitch = static_cast<int16>(event.key());
			e.noteOff.velocity = event.velocity() / 127.f;
			e.noteOff.noteId = -1;
			e.noteOff.tuning = 0.f;
			std::lock_guard<std::mutex> lock{m_midiMutex};
			m_pendingEvents.push_back(e);
			break;
		}
		case MidiEventTypes::MidiPitchBend:
			queueMidiCc(event.channel(), kPitchBend, event.pitchBend() / 16383., offset);
			break;
		case MidiEventTypes::MidiControlChange:
			queueMidiCc(event.channel(), event.controllerNumber(),
				event.controllerValue() / 127., offset);
			break;
		case MidiEventTypes::MidiChannelPressure:
			queueMidiCc(event.channel(), kAfterTouch, (event.param(0) & 0x7F) / 127., offset);
			break;
		default:
			break;
	}
}




void Vst3Plugin::queueMidiCc(int channel, int cc, double value, f_cnt_t)
{
	if (!m_midiMapping) { return; }

	Vst::ParamID id = Vst::kNoParamId;
	if (m_midiMapping->getMidiControllerAssignment(0, static_cast<int16>(channel),
			static_cast<Vst::CtrlNumber>(cc), id) != kResultOk || id == Vst::kNoParamId)
	{
		return;
	}

	std::lock_guard<std::mutex> lock{m_midiMutex};
	m_pendingCc.emplace_back(id, value);
}




void Vst3Plugin::updateProcessContext(f_cnt_t frames)
{
	using namespace Vst;

	Song* song = Engine::getSong();
	m_context = {};
	m_context.state = ProcessContext::kTempoValid | ProcessContext::kTimeSigValid
		| ProcessContext::kContTimeValid | ProcessContext::kProjectTimeMusicValid;
	if (song->isPlaying()) { m_context.state |= ProcessContext::kPlaying; }
	m_context.sampleRate = m_sampleRate;
	m_context.projectTimeSamples = song->getFrames();
	m_context.continousTimeSamples = static_cast<TSamples>(m_continuousSamples);
	m_context.tempo = song->getTempo();
	m_context.timeSigNumerator = song->getTimeSigModel().getNumerator();
	m_context.timeSigDenominator = song->getTimeSigModel().getDenominator();
	m_context.projectTimeMusic =
		double(m_context.projectTimeSamples) / m_sampleRate * m_context.tempo / 60.;

	m_continuousSamples += frames;
}




void Vst3Plugin::process(const SampleFrame* in, SampleFrame* out, f_cnt_t frames)
{
	std::unique_lock<std::mutex> lock{m_processMutex, std::try_to_lock};
	if (!lock.owns_lock() || !m_processing || frames > m_blockSize)
	{
		if (out) { zeroSampleFrames(out, frames); }
		return;
	}

	m_events->clear();
	m_outEvents->clear();
	m_inChanges->clear();
	m_outChanges->clear();

	{
		std::lock_guard<std::mutex> midiLock{m_midiMutex};
		for (auto& e : m_pendingEvents)
		{
			e.sampleOffset = std::clamp<int32>(e.sampleOffset, 0, frames - 1);
			m_events->addEvent(e);
		}
		m_pendingEvents.clear();

		for (const auto& [id, value] : m_pendingCc)
		{
			int32 index = 0;
			auto queue = m_inChanges->addParameterData(id, index);
			if (queue)
			{
				int32 pointIndex = 0;
				queue->addPoint(0, value, pointIndex);
			}
		}
		m_pendingCc.clear();
	}

	for (const auto& param : m_params)
	{
		if (param->dirty.exchange(false))
		{
			int32 index = 0;
			auto queue = m_inChanges->addParameterData(param->id, index);
			if (queue)
			{
				int32 pointIndex = 0;
				queue->addPoint(0, std::clamp(param->pendingValue.load(), 0., 1.),
					pointIndex);
			}
		}
	}

	// feed audio input into the main input bus
	if (m_mainIn >= 0)
	{
		AudioBus& bus = m_inBuses[m_mainIn];
		if (in)
		{
			for (int ch = 0; ch < bus.channels; ++ch)
			{
				float* dst = bus.pointers[ch];
				if (bus.channels == 1)
				{
					for (f_cnt_t f = 0; f < frames; ++f)
					{
						dst[f] = (in[f].left() + in[f].right()) * 0.5f;
					}
				}
				else
				{
					const int src = ch < 2 ? ch : ch % 2;
					for (f_cnt_t f = 0; f < frames; ++f) { dst[f] = in[f][src]; }
				}
			}
			m_inBusBuffers[m_mainIn].silenceFlags = 0;
		}
		else
		{
			for (int ch = 0; ch < bus.channels; ++ch)
			{
				std::memset(bus.pointers[ch], 0, frames * sizeof(float));
			}
			m_inBusBuffers[m_mainIn].silenceFlags = (uint64{1} << bus.channels) - 1;
		}
	}

	updateProcessContext(frames);

	Vst::ProcessData data;
	data.processMode = Vst::kRealtime;
	data.symbolicSampleSize = Vst::kSample32;
	data.numSamples = frames;
	data.numInputs = static_cast<int32>(m_inBusBuffers.size());
	data.numOutputs = static_cast<int32>(m_outBusBuffers.size());
	data.inputs = m_inBusBuffers.empty() ? nullptr : m_inBusBuffers.data();
	data.outputs = m_outBusBuffers.empty() ? nullptr : m_outBusBuffers.data();
	data.inputParameterChanges = m_inChanges.get();
	data.outputParameterChanges = m_outChanges.get();
	data.inputEvents = m_events.get();
	data.outputEvents = m_outEvents.get();
	data.processContext = &m_context;

	const tresult result = m_processor->process(data);

	if (out)
	{
		if (result == kResultOk && m_mainOut >= 0)
		{
			const AudioBus& bus = m_outBuses[m_mainOut];
			const float* left = bus.pointers[0];
			const float* right = bus.channels > 1 ? bus.pointers[1] : bus.pointers[0];
			for (f_cnt_t f = 0; f < frames; ++f)
			{
				out[f].setLeft(left[f]);
				out[f].setRight(right[f]);
			}
		}
		else
		{
			zeroSampleFrames(out, frames);
		}
	}
}




void Vst3Plugin::saveSettings(QDomDocument& doc, QDomElement& elem)
{
	elem.setAttribute("uid", m_classInfo.uid);
	elem.setAttribute("plugin-file", m_classInfo.modulePath);

	auto stream = owned(new MemoryStream());
	if (m_component->getState(stream) == kResultOk && !stream->data().isEmpty())
	{
		QString b64;
		base64::encode(stream->data().constData(), stream->data().size(), b64);
		elem.setAttribute("chunk", b64);
	}

	if (m_controller)
	{
		auto ctrlStream = owned(new MemoryStream());
		if (m_controller->getState(ctrlStream) == kResultOk && !ctrlStream->data().isEmpty())
		{
			QString b64;
			base64::encode(ctrlStream->data().constData(), ctrlStream->data().size(), b64);
			elem.setAttribute("ctrlchunk", b64);
		}
	}

	// save models of automated / changed parameters so automation targets
	// can be restored
	QDomElement params = doc.createElement("params");
	elem.appendChild(params);
	for (const auto& param : m_params)
	{
		if (param->model->isAutomatedOrControlled()
			|| param->model->value() != static_cast<float>(param->defaultNormalized))
		{
			param->model->saveSettings(doc, params, QString("param%1").arg(param->id));
		}
	}
}




void Vst3Plugin::loadSettings(const QDomElement& elem)
{
	const QString chunk = elem.attribute("chunk");
	if (!chunk.isEmpty())
	{
		const QByteArray data = QByteArray::fromBase64(chunk.toUtf8());
		auto stream = owned(new MemoryStream(data));

		std::lock_guard<std::mutex> lock{m_processMutex};
		m_component->setState(stream);
		if (m_controller)
		{
			stream->rewind();
			m_controller->setComponentState(stream);
		}
	}

	const QString ctrlChunk = elem.attribute("ctrlchunk");
	if (!ctrlChunk.isEmpty() && m_controller)
	{
		const QByteArray data = QByteArray::fromBase64(ctrlChunk.toUtf8());
		auto stream = owned(new MemoryStream(data));
		m_controller->setState(stream);
	}

	refreshModelsFromController(false);

	// only load models that were actually saved - loadSettings() resets
	// models it can not find, which would wipe the chunk-restored values
	const QDomElement params = elem.firstChildElement("params");
	if (!params.isNull())
	{
		for (QDomElement node = params.firstChildElement(); !node.isNull();
			node = node.nextSiblingElement())
		{
			const QString name = node.hasAttribute("nodename")
				? node.attribute("nodename") : node.nodeName();
			if (!name.startsWith("param")) { continue; }
			bool ok = false;
			const auto id = name.mid(5).toUInt(&ok);
			if (!ok) { continue; }
			const auto it = m_paramById.find(id);
			if (it != m_paramById.end())
			{
				it->second->model->loadSettings(params, name);
			}
		}
	}
}




bool Vst3Plugin::hasEditor()
{
	if (m_hasEditorCached < 0)
	{
		auto view = createEditorView();
		m_hasEditorCached = view ? 1 : 0;
	}
	return m_hasEditorCached == 1;
}




IPtr<IPlugView> Vst3Plugin::createEditorView()
{
	if (!m_controller) { return nullptr; }
	return owned(m_controller->createView(Vst::ViewType::kEditor));
}


} // namespace lmms::vst3
