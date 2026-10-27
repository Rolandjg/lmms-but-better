/*
 * FigglebottomTuneControls.cpp - controls for the FigglebottomTune pitch corrector
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

#include "FigglebottomTuneControls.h"

#include <QDomElement>

#include "FigglebottomTune.h"

namespace lmms
{

namespace
{

constexpr const char* NoteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

//! Every field not given keeps the default value. Presets leave the key and scale alone.
struct Preset
{
	const char* name;
	float retune = 25.f;
	float glide = 0.f;
	float amount = 100.f;
	float humanize = 0.f;
	float sticky = 15.f;
	float tolerance = 0.f;
	float tracking = 50.f;
	float shift = 0.f;
	float fine = 0.f;
	float mix = 100.f;
	float gain = 0.f;
};

const std::array Presets{
	Preset{.name = QT_TRANSLATE_NOOP("FigglebottomTuneControls", "Init")},
	Preset{.name = QT_TRANSLATE_NOOP("FigglebottomTuneControls", "Natural Touch"), .retune = 120.f, .humanize = 60.f,
		.sticky = 20.f, .tolerance = 15.f},
	Preset{.name = QT_TRANSLATE_NOOP("FigglebottomTuneControls", "Gentle Pop"), .retune = 50.f, .humanize = 35.f},
	Preset{.name = QT_TRANSLATE_NOOP("FigglebottomTuneControls", "Modern Pop"), .retune = 12.f, .humanize = 15.f},
	Preset{.name = QT_TRANSLATE_NOOP("FigglebottomTuneControls", "Ballad (Keep Vibrato)"), .retune = 40.f,
		.humanize = 85.f, .sticky = 25.f},
	Preset{.name = QT_TRANSLATE_NOOP("FigglebottomTuneControls", "Hard Tune"), .retune = 0.f, .sticky = 10.f},
	Preset{.name = QT_TRANSLATE_NOOP("FigglebottomTuneControls", "Robot Glide"), .retune = 0.f, .glide = 60.f},
	Preset{.name = QT_TRANSLATE_NOOP("FigglebottomTuneControls", "Stepped Robot"), .retune = 0.f, .sticky = 40.f},
	Preset{.name = QT_TRANSLATE_NOOP("FigglebottomTuneControls", "Half Correction"), .retune = 30.f, .amount = 50.f},
	Preset{.name = QT_TRANSLATE_NOOP("FigglebottomTuneControls", "Octave Up Sparkle"), .retune = 0.f, .shift = 12.f},
	Preset{.name = QT_TRANSLATE_NOOP("FigglebottomTuneControls", "Octave Down Double"), .retune = 20.f,
		.shift = -12.f, .mix = 50.f},
	Preset{.name = QT_TRANSLATE_NOOP("FigglebottomTuneControls", "Fifth Harmony"), .retune = 15.f, .shift = 7.f,
		.mix = 50.f},
	Preset{.name = QT_TRANSLATE_NOOP("FigglebottomTuneControls", "Detuned Double"), .retune = 30.f, .fine = 12.f,
		.mix = 50.f},
};

} // namespace


QString FigglebottomTuneControls::noteName(int note)
{
	return QString("%1%2").arg(NoteNames[figglebottomtune::pitchClass(note)])
		.arg(static_cast<int>(std::floor(note / 12.f)) - 1);
}


FigglebottomTuneControls::FigglebottomTuneControls(FigglebottomTuneEffect* effect) :
	EffectControls(effect),
	m_effect(effect),
	m_presetModel(this, tr("Preset")),
	m_keyModel(this, tr("Key")),
	m_scaleModel(this, tr("Scale")),
	m_retuneModel(25.f, 0.f, 400.f, 0.1f, this, tr("Retune speed")),
	m_glideModel(0.f, 0.f, 500.f, 0.1f, this, tr("Note glide")),
	m_amountModel(100.f, 0.f, 100.f, 0.1f, this, tr("Amount")),
	m_humanizeModel(0.f, 0.f, 100.f, 0.1f, this, tr("Humanize")),
	m_stickyModel(15.f, 0.f, 50.f, 0.1f, this, tr("Sticky")),
	m_toleranceModel(0.f, 0.f, 50.f, 0.1f, this, tr("Tolerance")),
	m_trackingModel(50.f, 0.f, 100.f, 0.1f, this, tr("Tracking")),
	m_shiftModel(0.f, -12.f, 12.f, 1.f, this, tr("Shift")),
	m_fineModel(0.f, -100.f, 100.f, 1.f, this, tr("Fine tune")),
	m_mixModel(100.f, 0.f, 100.f, 0.1f, this, tr("Mix")),
	m_gainModel(0.f, -24.f, 24.f, 0.1f, this, tr("Output gain"))
{
	for (const auto& preset : Presets) { m_presetModel.addItem(tr(preset.name)); }
	for (const auto name : NoteNames) { m_keyModel.addItem(name); }
	for (const auto& scale : figglebottomtune::Scales) { m_scaleModel.addItem(tr(scale.name)); }
	m_scaleModel.setValue(1); // Major
	for (int i = 0; i < figglebottomtune::PitchClasses; ++i)
	{
		m_noteModels[i] = new BoolModel(true, this, tr("Note %1").arg(NoteNames[i]));
		connect(m_noteModels[i], &Model::dataChanged, this, [this] {
			if (!m_syncingNotes) { updateScaleFromNotes(); }
		});
	}
	applyScale();

	m_retuneModel.setScaleLogarithmic(true);
	m_glideModel.setScaleLogarithmic(true);
	m_shiftModel.setCenterValue(0.f);
	m_fineModel.setCenterValue(0.f);
	m_gainModel.setCenterValue(0.f);

	connect(&m_keyModel, &Model::dataChanged, this, [this] { if (!m_syncingNotes) { applyScale(); } });
	connect(&m_scaleModel, &Model::dataChanged, this, [this] { if (!m_syncingNotes) { applyScale(); } });
	connect(&m_presetModel, &Model::dataChanged, this, [this] {
		if (!m_loadingPreset) { applyPreset(m_presetModel.value()); }
	});
}


void FigglebottomTuneControls::applyScale()
{
	if (m_scaleModel.value() == figglebottomtune::CustomScale) { return; }
	const auto mask = figglebottomtune::scaleMask(m_keyModel.value(), m_scaleModel.value());
	m_syncingNotes = true;
	for (int i = 0; i < figglebottomtune::PitchClasses; ++i) { m_noteModels[i]->setValue(mask[i]); }
	m_syncingNotes = false;
}


void FigglebottomTuneControls::updateScaleFromNotes()
{
	// Hand-picked notes: show "Custom" unless they happen to spell the selected scale
	figglebottomtune::NoteMask notes{};
	for (int i = 0; i < figglebottomtune::PitchClasses; ++i) { notes[i] = noteEnabled(i); }
	if (m_scaleModel.value() != figglebottomtune::CustomScale
		&& figglebottomtune::scaleMask(m_keyModel.value(), m_scaleModel.value()) == notes)
	{
		return;
	}
	m_syncingNotes = true;
	m_scaleModel.setValue(figglebottomtune::CustomScale);
	m_syncingNotes = false;
}


void FigglebottomTuneControls::toggleNote(int pitchClass)
{
	setNote(pitchClass, !noteEnabled(pitchClass));
}


void FigglebottomTuneControls::setNote(int pitchClass, bool on)
{
	m_noteModels[figglebottomtune::pitchClass(pitchClass)]->setValue(on);
}


void FigglebottomTuneControls::applyPreset(int index)
{
	if (index < 0 || index >= static_cast<int>(Presets.size())) { return; }
	const auto& p = Presets[index];
	m_retuneModel.setValue(p.retune);
	m_glideModel.setValue(p.glide);
	m_amountModel.setValue(p.amount);
	m_humanizeModel.setValue(p.humanize);
	m_stickyModel.setValue(p.sticky);
	m_toleranceModel.setValue(p.tolerance);
	m_trackingModel.setValue(p.tracking);
	m_shiftModel.setValue(p.shift);
	m_fineModel.setValue(p.fine);
	m_mixModel.setValue(p.mix);
	m_gainModel.setValue(p.gain);
}


void FigglebottomTuneControls::loadSettings(const QDomElement& parent)
{
	// The preset index is only a label, and the key and scale must not overwrite the saved notes
	m_loadingPreset = true;
	m_presetModel.loadSettings(parent, "preset");
	m_loadingPreset = false;
	m_syncingNotes = true;
	m_keyModel.loadSettings(parent, "key");
	m_scaleModel.loadSettings(parent, "scale");
	for (int i = 0; i < figglebottomtune::PitchClasses; ++i)
	{
		m_noteModels[i]->loadSettings(parent, QString("note%1").arg(i));
	}
	m_syncingNotes = false;
	m_retuneModel.loadSettings(parent, "retune");
	m_glideModel.loadSettings(parent, "glide");
	m_amountModel.loadSettings(parent, "amount");
	m_humanizeModel.loadSettings(parent, "humanize");
	m_stickyModel.loadSettings(parent, "sticky");
	m_toleranceModel.loadSettings(parent, "tolerance");
	m_trackingModel.loadSettings(parent, "tracking");
	m_shiftModel.loadSettings(parent, "shift");
	m_fineModel.loadSettings(parent, "fine");
	m_mixModel.loadSettings(parent, "mix");
	m_gainModel.loadSettings(parent, "gain");
}


void FigglebottomTuneControls::saveSettings(QDomDocument& doc, QDomElement& parent)
{
	m_presetModel.saveSettings(doc, parent, "preset");
	m_keyModel.saveSettings(doc, parent, "key");
	m_scaleModel.saveSettings(doc, parent, "scale");
	for (int i = 0; i < figglebottomtune::PitchClasses; ++i)
	{
		m_noteModels[i]->saveSettings(doc, parent, QString("note%1").arg(i));
	}
	m_retuneModel.saveSettings(doc, parent, "retune");
	m_glideModel.saveSettings(doc, parent, "glide");
	m_amountModel.saveSettings(doc, parent, "amount");
	m_humanizeModel.saveSettings(doc, parent, "humanize");
	m_stickyModel.saveSettings(doc, parent, "sticky");
	m_toleranceModel.saveSettings(doc, parent, "tolerance");
	m_trackingModel.saveSettings(doc, parent, "tracking");
	m_shiftModel.saveSettings(doc, parent, "shift");
	m_fineModel.saveSettings(doc, parent, "fine");
	m_mixModel.saveSettings(doc, parent, "mix");
	m_gainModel.saveSettings(doc, parent, "gain");
}

} // namespace lmms
