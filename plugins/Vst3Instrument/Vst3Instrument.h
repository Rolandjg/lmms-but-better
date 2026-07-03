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

namespace lmms
{

namespace gui
{
class Vst3InsView;
class Vst3PluginWidget;
}


class Vst3Instrument : public Instrument
{
	Q_OBJECT

public:
	Vst3Instrument(InstrumentTrack* track, Descriptor::SubPluginFeatures::Key* key);
	~Vst3Instrument() override;

	void saveSettings(QDomDocument& doc, QDomElement& elem) override;
	void loadSettings(const QDomElement& elem) override;

	bool hasNoteInput() const override { return m_plugin->hasNoteInput(); }
	bool handleMidiEvent(const MidiEvent& event, const TimePos& time = TimePos(),
		f_cnt_t offset = 0) override;
	void play(SampleFrame* buf) override;

	gui::PluginView* instantiateView(QWidget* parent) override;

	QString nodeName() const override { return "vst3instrument"; }

private slots:
	void onSampleRateChanged();

private:
	std::unique_ptr<vst3::Vst3Plugin> m_plugin;

	friend class gui::Vst3InsView;
};


namespace gui
{

class Vst3InsView : public InstrumentView
{
	Q_OBJECT

public:
	Vst3InsView(Vst3Instrument* instrument, QWidget* parent);

private:
	void modelChanged() override;

	Vst3PluginWidget* m_widget = nullptr;
};

} // namespace gui


} // namespace lmms

#endif // LMMS_VST3_INSTRUMENT_H
