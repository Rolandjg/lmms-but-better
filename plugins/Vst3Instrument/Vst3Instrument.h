/*
 * Vst3Instrument.h - instrument plugin hosting VST3 instruments
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

#ifndef LMMS_VST3_INSTRUMENT_H
#define LMMS_VST3_INSTRUMENT_H

#include <memory>

#include "Instrument.h"
#include "InstrumentView.h"
#include "Vst3Plugin.h"

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace lmms
{

namespace gui
{
class Vst3InsView;
class Vst3PluginWidget;
class Vst3EditorWindow;
class SubWindow;
}


class Vst3Instrument : public Instrument
{
	Q_OBJECT

public:
	//! The key may have an empty "uid" attribute: the instrument then
	//! starts empty and a plugin can be loaded via loadFile()
	Vst3Instrument(InstrumentTrack* track, Descriptor::SubPluginFeatures::Key* key);
	~Vst3Instrument() override;

	void saveSettings(QDomDocument& doc, QDomElement& elem) override;
	void loadSettings(const QDomElement& elem) override;

	//! Load (or replace) the hosted plugin from a .vst3 bundle / module file
	void loadFile(const QString& file) override;

	bool hasNoteInput() const override { return m_plugin && m_plugin->hasNoteInput(); }
	bool handleMidiEvent(const MidiEvent& event, const TimePos& time = TimePos(),
		f_cnt_t offset = 0) override;
	void play(SampleFrame* buf) override;

	gui::PluginView* instantiateView(QWidget* parent) override;

	QString nodeName() const override { return "vst3instrument"; }

	vst3::Vst3Plugin* plugin() { return m_plugin.get(); }

signals:
	//! Emitted after the hosted plugin was loaded or replaced
	void pluginChanged();

private slots:
	void onSampleRateChanged();

private:
	//! Swap in a new plugin instance, safe against the running audio thread
	void setPlugin(const QString& uid, const QString& file);

	std::unique_ptr<vst3::Vst3Plugin> m_plugin;

	friend class gui::Vst3InsView;
};


namespace gui
{

class Vst3InsView : public InstrumentViewFixedSize
{
	Q_OBJECT

public:
	Vst3InsView(Vst3Instrument* instrument, QWidget* parent);
	~Vst3InsView() override;

private slots:
	void openFileDialog();
	void toggleEditor(bool show);
	void toggleControls();
	void rebuild();

private:
	void modelChanged() override;
	void closePluginWindows();

	QVBoxLayout* m_layout = nullptr;
	QPushButton* m_openFileButton = nullptr;
	QPushButton* m_toggleUiButton = nullptr;
	QPushButton* m_controlsButton = nullptr;
	QLabel* m_pluginLabel = nullptr;
	QLabel* m_errorLabel = nullptr;
	Vst3EditorWindow* m_editorWindow = nullptr;
	SubWindow* m_controlsWindow = nullptr;
};

} // namespace gui


} // namespace lmms

#endif // LMMS_VST3_INSTRUMENT_H
