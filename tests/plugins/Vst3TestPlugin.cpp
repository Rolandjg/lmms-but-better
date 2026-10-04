/* Minimal, deterministic VST3 fixture: gain, note input and silence flags.
 * Copyright (c) 2026 LMMS developers. Licensed under GPL-2.0-or-later.
 */
#include <cstring>
#include <cstdlib>
#include <stdexcept>
#include <limits>
#include <thread>
#include "pluginterfaces/gui/iplugview.h"
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

using namespace Steinberg;
using namespace Steinberg::Vst;
namespace
{
int stats[8]{};
// active-on, active-off, processing-off, parameter queries, mapping queries,
// editor destruction, editor removal, timer callbacks
const FUID classId{0x15B2E311, 0xDFBC4BE2, 0x951D0921, 0x106501AB};
bool equal(const char* a, const FUID& b) { return std::memcmp(a, b, 16) == 0; }

class TestView final : public IPlugView, public Linux::ITimerHandler
{
	uint32 refs = 1;
public:
	~TestView() { ++stats[5]; }
	tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
	{
		*obj = nullptr;
		if (equal(iid, IPlugView::iid) || equal(iid, FUnknown::iid)) { *obj = static_cast<IPlugView*>(this); }
		else if (equal(iid, Linux::ITimerHandler::iid)) { *obj = static_cast<Linux::ITimerHandler*>(this); }
		if (!*obj) { return kNoInterface; }
		addRef(); return kResultOk;
	}
	uint32 PLUGIN_API addRef() override { return ++refs; }
	uint32 PLUGIN_API release() override { auto n = --refs; if (!n) { delete this; } return n; }
	tresult PLUGIN_API isPlatformTypeSupported(FIDString) override { return kResultTrue; }
	tresult PLUGIN_API attached(void*, FIDString) override
	{
		if (std::getenv("LMMS_TEST_VST3_ATTACH_THROW")) { throw std::runtime_error("editor attachment threw"); }
		if (std::getenv("LMMS_TEST_VST3_ATTACH_FAIL")) { return kResultFalse; }
		return kResultOk;
	}
	tresult PLUGIN_API removed() override
	{
		++stats[6];
		if (std::getenv("LMMS_TEST_VST3_REMOVE_THROW")) { throw std::runtime_error("editor removal failed"); }
		return kResultOk;
	}
	tresult PLUGIN_API onWheel(float) override { return kResultOk; }
	tresult PLUGIN_API onKeyDown(char16, int16, int16) override { return kResultOk; }
	tresult PLUGIN_API onKeyUp(char16, int16, int16) override { return kResultOk; }
	tresult PLUGIN_API getSize(ViewRect* r) override
	{
		if (std::getenv("LMMS_TEST_VST3_SIZE_THROW")) { throw std::runtime_error("editor size threw"); }
		*r = {0, 0, 200, 100}; return kResultOk;
	}
	tresult PLUGIN_API onSize(ViewRect*) override
	{
		if (std::getenv("LMMS_TEST_VST3_RESIZE_THROW")) { throw std::runtime_error("editor resize threw"); }
		return kResultOk;
	}
	tresult PLUGIN_API onFocus(TBool) override { return kResultOk; }
	tresult PLUGIN_API setFrame(IPlugFrame* frame) override
	{
#ifndef __APPLE__
		if (frame)
		{
			Linux::IRunLoop* loop = nullptr;
			if (frame->queryInterface(Linux::IRunLoop::iid, reinterpret_cast<void**>(&loop)) == kResultOk)
			{
				loop->registerTimer(this, 1);
				loop->release();
			}
		}
#endif
		// Deliberately leave cleanup to the host, including on failed attach.
		return kResultOk;
	}
	tresult PLUGIN_API canResize() override { return kResultTrue; }
	tresult PLUGIN_API checkSizeConstraint(ViewRect*) override { return kResultTrue; }
	void PLUGIN_API onTimer() override
	{
		++stats[7];
		if (std::getenv("LMMS_TEST_VST3_TIMER_THROW")) { throw std::runtime_error("editor timer failed"); }
	}
};

class TestPlugin final : public IComponent, public IAudioProcessor, public IEditController, public IMidiMapping
{
	uint32 refs = 1;
	std::thread::id uiThread;
	double gain = .5;
	bool note = false;
	double pitchBend = 8192. / 16383.;
public:
	tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
	{
		*obj = nullptr;
		if (equal(iid, IComponent::iid) || equal(iid, FUnknown::iid)) { *obj = static_cast<IComponent*>(this); }
		else if (equal(iid, IAudioProcessor::iid)) { *obj = static_cast<IAudioProcessor*>(this); }
		else if (equal(iid, IEditController::iid)) { *obj = static_cast<IEditController*>(this); }
		else if (equal(iid, IMidiMapping::iid)) { *obj = static_cast<IMidiMapping*>(this); }
		if (!*obj) { return kNoInterface; }
		addRef();
		return kResultOk;
	}
	uint32 PLUGIN_API addRef() override { return ++refs; }
	uint32 PLUGIN_API release() override { const auto n = --refs; if (!n) { delete this; } return n; }
	tresult PLUGIN_API initialize(FUnknown*) override { uiThread = std::this_thread::get_id(); return kResultOk; }
	tresult PLUGIN_API terminate() override { return kResultOk; }
	tresult PLUGIN_API getControllerClassId(TUID) override { return kResultFalse; }
	tresult PLUGIN_API setIoMode(IoMode) override { return kResultOk; }
	int32 PLUGIN_API getBusCount(MediaType type, BusDirection dir) override
	{
		if (std::getenv("LMMS_TEST_VST3_INVALID_BUSES")) { return 1000000; }
		return type == kAudio || dir == kInput ? 1 : 0;
	}
	tresult PLUGIN_API getBusInfo(MediaType type, BusDirection dir, int32 index, BusInfo& bus) override
	{
		if (index != 0) { return kInvalidArgument; }
		bus = {};
		bus.mediaType = type;
		bus.direction = dir;
		bus.channelCount = type == kAudio ? 2 : 16;
		bus.busType = kMain;
		bus.flags = BusInfo::kDefaultActive;
		return kResultOk;
	}
	tresult PLUGIN_API getRoutingInfo(RoutingInfo&, RoutingInfo&) override { return kNotImplemented; }
	tresult PLUGIN_API activateBus(MediaType, BusDirection, int32, TBool) override
	{ return std::getenv("LMMS_TEST_VST3_BUS_ACTIVATION_FAIL") ? kResultFalse : kResultOk; }
	tresult PLUGIN_API setActive(TBool active) override { ++stats[active ? 0 : 1]; return kResultOk; }
	tresult PLUGIN_API setState(IBStream* stream) override { return stream->read(&gain, sizeof(gain)); }
	tresult PLUGIN_API getState(IBStream* stream) override { return stream->write(&gain, sizeof(gain)); }
	tresult PLUGIN_API setComponentState(IBStream* stream) override { return setState(stream); }
	tresult PLUGIN_API setBusArrangements(SpeakerArrangement*, int32, SpeakerArrangement*, int32) override
	{ return std::getenv("LMMS_TEST_VST3_ARRANGEMENT_FAIL") ? kResultFalse : kResultOk; }
	tresult PLUGIN_API getBusArrangement(BusDirection, int32, SpeakerArrangement& arr) override
	{
		if (std::getenv("LMMS_TEST_VST3_ARRANGEMENT_FAIL")) { return kResultFalse; }
		arr = SpeakerArr::kStereo; return kResultOk;
	}
	tresult PLUGIN_API canProcessSampleSize(int32 size) override { return size == kSample32 ? kResultOk : kResultFalse; }
	uint32 PLUGIN_API getLatencySamples() override { return 0; }
	tresult PLUGIN_API setupProcessing(ProcessSetup&) override { return kResultOk; }
	tresult PLUGIN_API setProcessing(TBool processing) override
	{
		if (!processing) { ++stats[2]; }
		if (std::getenv(processing ? "LMMS_TEST_VST3_START_THROW" : "LMMS_TEST_VST3_STOP_THROW"))
		{ throw std::runtime_error("processing transition failed"); }
		return kResultOk;
	}
	uint32 PLUGIN_API getTailSamples() override { return 0; }
	tresult PLUGIN_API process(ProcessData& data) override
	{
		if (const char* failure = std::getenv("LMMS_TEST_VST3_PROCESS_FAILURE"))
		{
			if (std::strcmp(failure, "throw") == 0) { throw std::runtime_error("test plugin process exception"); }
			if (std::strcmp(failure, "error") == 0) { return kResultFalse; }
		}
		if (data.inputParameterChanges)
		{
			for (int i = 0; i < data.inputParameterChanges->getParameterCount(); ++i)
			{
				auto queue = data.inputParameterChanges->getParameterData(i);
				int32 offset;
				if (queue->getParameterId() == 7) { queue->getPoint(queue->getPointCount() - 1, offset, gain); }
			}
		}
		if (gain == 0)
		{
			// Intentionally leave old samples in the buffer. The host must
			// honor the silence flags rather than replaying stale audio.
			data.outputs[0].silenceFlags = 3;
			return kResultOk;
		}
		int next = 0;
		Event event{};
		const auto eventCount = data.inputEvents ? data.inputEvents->getEventCount() : 0;
		if (eventCount) { data.inputEvents->getEvent(0, event); }
		for (int f = 0; f < data.numSamples; ++f)
		{
			if (data.inputParameterChanges)
			{
				for (int i = 0; i < data.inputParameterChanges->getParameterCount(); ++i)
				{
					auto queue = data.inputParameterChanges->getParameterData(i);
					if (queue->getParameterId() != 8) { continue; }
					for (int p = 0; p < queue->getPointCount(); ++p)
					{
						int32 offset;
						double value;
						queue->getPoint(p, offset, value);
						if (offset == f) { pitchBend = value; }
					}
				}
			}
			while (next < eventCount && event.sampleOffset <= f)
			{
				note = event.type == Event::kNoteOnEvent;
				if (++next < eventCount) { data.inputEvents->getEvent(next, event); }
			}
			for (int ch = 0; ch < 2; ++ch)
			{
				data.outputs[0].channelBuffers32[ch][f] =
					(data.inputs[0].channelBuffers32[ch][f] + (note ? .25f : 0.f)) * gain
					+ (note ? pitchBend - 8192. / 16383. : 0.);
			}
		}
		return kResultOk;
	}
	int32 PLUGIN_API getParameterCount() override { return std::getenv("LMMS_TEST_VST3_HUGE_PARAMS") ? 1000000000 : 1; }
	tresult PLUGIN_API getMidiControllerAssignment(int32, int16, CtrlNumber controller, ParamID& id) override
	{
		++stats[4];
		if (std::this_thread::get_id() != uiThread || std::getenv("LMMS_TEST_VST3_MAPPING_FORBIDDEN"))
		{ throw std::runtime_error("mapping queried during MIDI delivery"); }
		if (controller != kPitchBend) { return kResultFalse; }
		id = std::getenv("LMMS_TEST_VST3_MAPPING_CHANGED") ? 7 : 8;
		return kResultOk;
	}
	tresult PLUGIN_API getParameterInfo(int32 index, ParameterInfo& info) override
	{
		++stats[3];
		if (index) { return kInvalidArgument; }
		info = {};
		info.id = 7;
		info.defaultNormalizedValue = .5;
		info.flags = ParameterInfo::kCanAutomate;
		std::memcpy(info.title, u"Gain", sizeof(u"Gain"));
		if (std::getenv("LMMS_TEST_VST3_UNTERMINATED_TITLE"))
		{ for (auto& c : info.title) { c = u'X'; } }
		return kResultOk;
	}
	tresult PLUGIN_API getParamStringByValue(ParamID, ParamValue, String128) override { return kNotImplemented; }
	tresult PLUGIN_API getParamValueByString(ParamID, TChar*, ParamValue&) override { return kNotImplemented; }
	ParamValue PLUGIN_API normalizedParamToPlain(ParamID, ParamValue v) override { return v; }
	ParamValue PLUGIN_API plainParamToNormalized(ParamID, ParamValue v) override { return v; }
	ParamValue PLUGIN_API getParamNormalized(ParamID) override
	{ return std::getenv("LMMS_TEST_VST3_NAN_PARAM") ? std::numeric_limits<double>::quiet_NaN() : gain; }
	tresult PLUGIN_API setParamNormalized(ParamID, ParamValue v) override { gain = v; return kResultOk; }
	tresult PLUGIN_API setComponentHandler(IComponentHandler*) override { return kResultOk; }
	IPlugView* PLUGIN_API createView(FIDString) override { return std::getenv("LMMS_TEST_VST3_EDITOR") ? new TestView : nullptr; }
};

class Factory : public IPluginFactory2
{
public:
	tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
	{
		*obj = nullptr;
		if (!equal(iid, IPluginFactory2::iid) && !equal(iid, IPluginFactory::iid) && !equal(iid, FUnknown::iid)) { return kNoInterface; }
		*obj = static_cast<IPluginFactory2*>(this);
		return kResultOk;
	}
	uint32 PLUGIN_API addRef() override { return 1; }
	uint32 PLUGIN_API release() override { return 1; }
	int32 PLUGIN_API countClasses() override { return 1; }
	tresult PLUGIN_API getFactoryInfo(PFactoryInfo* info) override { *info = {}; std::strcpy(info->vendor, "LMMS tests"); return kResultOk; }
	tresult PLUGIN_API getClassInfo(int32, PClassInfo*) override { return kNotImplemented; }
	tresult PLUGIN_API getClassInfo2(int32 index, PClassInfo2* info) override
	{
		if (index) { return kInvalidArgument; }
		*info = {};
		std::memcpy(info->cid, classId, 16);
		std::strcpy(info->name, "LMMS VST3 test gain");
		std::strcpy(info->category, kVstAudioEffectClass);
		std::strcpy(info->subCategories, "Fx");
		return kResultOk;
	}
	tresult PLUGIN_API createInstance(FIDString cid, FIDString iid, void** obj) override
	{
		if (!equal(cid, classId)) { return kInvalidArgument; }
		auto plugin = new TestPlugin;
		const auto result = plugin->queryInterface(iid, obj);
		plugin->release();
		return result;
	}
};
}
extern "C" __attribute__((visibility("default"))) IPluginFactory* GetPluginFactory()
{
	static Factory factory;
	return &factory;
}
extern "C" __attribute__((visibility("default"))) bool ModuleEntry(void*)
{
	if (std::getenv("LMMS_TEST_VST3_CRASH")) { std::abort(); }
	return true;
}
extern "C" __attribute__((visibility("default"))) bool ModuleExit() { return true; }

#ifdef __APPLE__
extern "C" __attribute__((visibility("default"))) bool bundleEntry(CFBundleRef bundle)
{
	// Verify the host passes the bundle, not a dlopen handle.
	if (!bundle || CFGetTypeID(bundle) != CFBundleGetTypeID()) { return false; }
	auto identifier = CFBundleGetIdentifier(bundle);
	if (!identifier || !CFEqual(identifier, CFSTR("io.lmms.vst3-test"))) { return false; }
	return ModuleEntry(nullptr);
}
extern "C" __attribute__((visibility("default"))) bool bundleExit() { return ModuleExit(); }
#endif

extern "C" __attribute__((visibility("default"))) int LMMS_TestVst3Stat(int index)
{ return stats[index]; }
extern "C" __attribute__((visibility("default"))) void LMMS_TestVst3ResetStats()
{ for (auto& value : stats) { value = 0; } }
