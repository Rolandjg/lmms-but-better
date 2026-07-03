/*
 * Vst3Plugin.h - a loaded instance of a VST3 plugin
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

#ifndef LMMS_VST3_PLUGIN_H
#define LMMS_VST3_PLUGIN_H

#include <QObject>
#include <QString>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include "AutomatableModel.h"
#include "LmmsTypes.h"
#include "Vst3Manager.h"

#include "vst3base_export.h"

class QDomDocument;
class QDomElement;
class QTimer;

namespace lmms
{

class MidiEvent;
class SampleFrame;
class TimePos;

namespace vst3
{

class Vst3ComponentHandler;
class Vst3EventList;
class Vst3Module;
class Vst3ParamChanges;


//! One loaded VST3 plugin instance: component + controller + processor,
//! LMMS models for its parameters, and audio/event processing
class VST3BASE_EXPORT Vst3Plugin : public QObject
{
	Q_OBJECT

public:
	struct Param
	{
		Steinberg::Vst::ParamID id = 0;
		QString title;
		QString shortTitle;
		QString unit;
		double defaultNormalized = 0.;
		FloatModel* model = nullptr;         //!< normalized [0..1]
		std::atomic<double> pendingValue{0.}; //!< latest value for the processor
		std::atomic<bool> dirty{false};       //!< pendingValue not yet sent to processor
		std::atomic<bool> controllerDirty{false}; //!< controller (plugin GUI) not yet informed
	};

	//! Load the plugin class @p uid; @p fileHint is a path to the module
	//! used when the uid is not found in the standard search paths.
	//! Throws std::runtime_error on failure.
	Vst3Plugin(Model* model, const QString& uid, const QString& fileHint);
	~Vst3Plugin() override;

	/*
		info
	*/
	const Vst3ClassInfo& classInfo() const { return m_classInfo; }
	const QString& name() const { return m_classInfo.name; }
	bool hasNoteInput() const { return m_hasEventInput; }

	/*
		realtime functions (audio thread)
	*/
	//! Run one period. @p in may be nullptr (no audio input); @p out may be
	//! nullptr (no interest in output). Buffers may not alias.
	void process(const SampleFrame* in, SampleFrame* out, f_cnt_t frames);
	//! Thread-safe enqueueing of MIDI events for the next period
	void handleMidiInputEvent(const MidiEvent& event, const TimePos& time, f_cnt_t offset);

	/*
		parameters
	*/
	std::size_t paramCount() const { return m_params.size(); }
	Param* param(std::size_t idx) { return m_params[idx].get(); }

	/*
		load/save
	*/
	void saveSettings(QDomDocument& doc, QDomElement& elem);
	void loadSettings(const QDomElement& elem);

	/*
		GUI (GUI thread only)
	*/
	bool hasEditor();
	//! Create the plugin's editor view; may return nullptr
	Steinberg::IPtr<Steinberg::IPlugView> createEditorView();

	//! Re-setup processing after sample rate / period size changes
	void reconfigure();

	/*
		host callbacks, do not call directly
	*/
	void parameterEditedByGui(Steinberg::Vst::ParamID id, double normalized);
	void componentRestartRequested(Steinberg::int32 flags);

signals:
	void pluginModelChanged();

private:
	void initParameters();
	void setupBuses();
	void setupProcessing();
	void allocateBuffers();
	void activate();
	void deactivate();
	void updateProcessContext(f_cnt_t frames);
	void refreshModelsFromController(bool pushToProcessor);
	void queueMidiCc(int channel, int cc, double value, f_cnt_t offset);
	void onModelChanged(Param* param);
	void syncControllerFromModels();

	Model* m_model;
	Vst3ClassInfo m_classInfo;
	std::shared_ptr<Vst3Module> m_module;

	Steinberg::IPtr<Steinberg::Vst::IComponent> m_component;
	Steinberg::IPtr<Steinberg::Vst::IEditController> m_controller;
	Steinberg::IPtr<Steinberg::Vst::IAudioProcessor> m_processor;
	Steinberg::IPtr<Steinberg::Vst::IMidiMapping> m_midiMapping;
	Steinberg::IPtr<Steinberg::Vst::IConnectionPoint> m_componentCP;
	Steinberg::IPtr<Steinberg::Vst::IConnectionPoint> m_controllerCP;
	Vst3ComponentHandler* m_handler = nullptr;
	bool m_singleComponent = false;
	bool m_componentInitialized = false;

	// parameters
	std::vector<std::unique_ptr<Param>> m_params;
	std::map<Steinberg::Vst::ParamID, Param*> m_paramById;
	bool m_settingFromController = false;
	QTimer* m_controllerSyncTimer = nullptr;

	// audio buses
	struct AudioBus
	{
		int channels = 0;
		bool isMain = false;
		std::vector<std::vector<float>> buffers;
		std::vector<float*> pointers;
	};
	std::vector<AudioBus> m_inBuses;
	std::vector<AudioBus> m_outBuses;
	std::vector<Steinberg::Vst::AudioBusBuffers> m_inBusBuffers;
	std::vector<Steinberg::Vst::AudioBusBuffers> m_outBusBuffers;
	int m_mainIn = -1;
	int m_mainOut = -1;
	bool m_hasEventInput = false;

	// events / parameter changes for processing
	std::unique_ptr<Vst3EventList> m_events;
	std::unique_ptr<Vst3EventList> m_outEvents;
	std::unique_ptr<Vst3ParamChanges> m_inChanges;
	std::unique_ptr<Vst3ParamChanges> m_outChanges;

	std::mutex m_midiMutex;
	std::vector<Steinberg::Vst::Event> m_pendingEvents;
	std::vector<std::pair<Steinberg::Vst::ParamID, double>> m_pendingCc;

	// processing state
	std::mutex m_processMutex;
	double m_sampleRate = 44100.;
	int m_blockSize = 256;
	bool m_processing = false;
	Steinberg::Vst::ProcessContext m_context = {};
	Steinberg::uint64 m_continuousSamples = 0;

	int m_hasEditorCached = -1;
};


} // namespace vst3

} // namespace lmms

#endif // LMMS_VST3_PLUGIN_H
