/*
 * FormantShifterControls.cpp - controls for the formant shifter / vowel filter
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

#include "FormantShifterControls.h"

#include <cmath>

#include <QDomElement>

#include "FormantShifter.h"

namespace lmms
{

namespace
{

struct SyncRate { const char* label; float ticks; };
constexpr std::array<SyncRate, 15> SyncRates{{
	{"Free", 0.f}, {"4 bars", 768.f}, {"2 bars", 384.f}, {"1 bar", 192.f}, {"1/2", 96.f}, {"1/4", 48.f},
	{"1/8", 24.f}, {"1/16", 12.f}, {"1/32", 6.f}, {"1/2 T", 64.f}, {"1/4 T", 32.f}, {"1/8 T", 16.f},
	{"1/16 T", 8.f}, {"1/4 D", 72.f}, {"1/8 D", 36.f}
}};

enum Sync { Free, Bars4, Bars2, Bar1, Half, Quarter, Eighth, Sixteenth, ThirtySecond };
enum Shape { Sine, Triangle, SawUp, SawDown, Square, SampleHold, SmoothRandom };
enum VoiceIndex { Bass, Tenor, Countertenor, Alto, Soprano };
constexpr int Spectral = 0;
constexpr int Resonant = 1;

//! Every field not given keeps the default (neutral) value
struct Preset
{
	const char* name;
	int mode = Spectral;
	float formant = 0.f;
	float pitch = 0.f;
	float fine = 0.f;
	bool preserve = true;
	float spread = 0.f;
	int voice = Tenor;
	float vowel = 0.f;
	float imprint = 0.f;
	float resonance = 1.f;
	float drive = 0.f;
	int lfoShape = Sine;
	int lfoSync = Free;
	float lfoRate = 1.f;
	float lfoVowel = 0.f;
	float lfoFormant = 0.f;
	float envVowel = 0.f;
	float envRelease = 150.f;
	float mix = 100.f;
	float gain = 0.f;
};

const std::array Presets{
	Preset{.name = QT_TRANSLATE_NOOP("FormantShifterControls", "Init")},
	// Voice changers: the sound's own formants, moved
	Preset{.name = QT_TRANSLATE_NOOP("FormantShifterControls", "Helium"), .formant = 6.f},
	Preset{.name = QT_TRANSLATE_NOOP("FormantShifterControls", "Chipmunk"), .pitch = 12.f, .preserve = false},
	Preset{.name = QT_TRANSLATE_NOOP("FormantShifterControls", "Tiny Voice"), .formant = 5.f, .pitch = 7.f},
	Preset{.name = QT_TRANSLATE_NOOP("FormantShifterControls", "Deep Voice"), .formant = -4.f, .pitch = -4.f},
	Preset{.name = QT_TRANSLATE_NOOP("FormantShifterControls", "Monster"), .formant = -7.f, .pitch = -12.f,
		.drive = 15.f},
	Preset{.name = QT_TRANSLATE_NOOP("FormantShifterControls", "Male to Female"), .formant = 2.5f, .pitch = 5.f},
	Preset{.name = QT_TRANSLATE_NOOP("FormantShifterControls", "Female to Male"), .formant = -2.5f, .pitch = -5.f},
	Preset{.name = QT_TRANSLATE_NOOP("FormantShifterControls", "Fifth Harmony"), .pitch = 7.f, .mix = 50.f},
	Preset{.name = QT_TRANSLATE_NOOP("FormantShifterControls", "Octave Down Double"), .pitch = -12.f, .mix = 50.f},
	Preset{.name = QT_TRANSLATE_NOOP("FormantShifterControls", "Wide Voices"), .spread = 4.f},
	Preset{.name = QT_TRANSLATE_NOOP("FormantShifterControls", "Throat Wobble"), .lfoShape = Sine,
		.lfoSync = Eighth, .lfoFormant = 4.f},
	// Talkbox: the vowel replaces the sound's formants
	Preset{.name = QT_TRANSLATE_NOOP("FormantShifterControls", "Talkbox Aah"), .vowel = 0.f, .imprint = 100.f,
		.drive = 35.f},
	Preset{.name = QT_TRANSLATE_NOOP("FormantShifterControls", "Talkbox Vowel Sweep"), .vowel = 2.f,
		.imprint = 100.f, .drive = 35.f, .lfoShape = Sine, .lfoSync = Bar1, .lfoVowel = 2.f},
	Preset{.name = QT_TRANSLATE_NOOP("FormantShifterControls", "Talk Wah (Envelope)"), .vowel = 4.f,
		.imprint = 100.f, .drive = 30.f, .envVowel = -4.f, .envRelease = 120.f},
	Preset{.name = QT_TRANSLATE_NOOP("FormantShifterControls", "Robot Choir"), .spread = 3.f,
		.voice = Countertenor, .vowel = 1.f, .imprint = 70.f, .drive = 20.f, .lfoShape = SmoothRandom,
		.lfoSync = Half, .lfoVowel = 1.2f},
	Preset{.name = QT_TRANSLATE_NOOP("FormantShifterControls", "Alien Chatter"), .formant = 3.f, .vowel = 2.f,
		.imprint = 100.f, .drive = 25.f, .lfoShape = SampleHold, .lfoSync = Sixteenth, .lfoVowel = 2.f,
		.lfoFormant = 2.f},
	// Resonant: the classic formant filter
	Preset{.name = QT_TRANSLATE_NOOP("FormantShifterControls", "Choir Aah"), .mode = Resonant, .spread = 2.f,
		.voice = Soprano, .vowel = 0.f, .resonance = 1.5f},
	Preset{.name = QT_TRANSLATE_NOOP("FormantShifterControls", "Ooh Pad"), .mode = Resonant, .voice = Alto,
		.vowel = 4.f, .resonance = 1.3f},
	Preset{.name = QT_TRANSLATE_NOOP("FormantShifterControls", "Eee Lead"), .mode = Resonant, .voice = Tenor,
		.vowel = 2.f, .drive = 40.f},
	Preset{.name = QT_TRANSLATE_NOOP("FormantShifterControls", "Yoi Bass"), .mode = Resonant, .voice = Bass,
		.vowel = 3.f, .resonance = 1.6f, .drive = 30.f, .lfoShape = SawDown, .lfoSync = Quarter, .lfoVowel = 1.f},
	Preset{.name = QT_TRANSLATE_NOOP("FormantShifterControls", "Wobble Talk 1/8"), .mode = Resonant,
		.vowel = 1.5f, .drive = 25.f, .lfoShape = Sine, .lfoSync = Eighth, .lfoVowel = 1.5f},
	Preset{.name = QT_TRANSLATE_NOOP("FormantShifterControls", "Auto-Wah Vowels"), .mode = Resonant,
		.vowel = 4.f, .resonance = 1.4f, .envVowel = -4.f, .envRelease = 200.f},
	Preset{.name = QT_TRANSLATE_NOOP("FormantShifterControls", "Random Babble"), .mode = Resonant,
		.voice = Countertenor, .vowel = 2.f, .resonance = 1.2f, .lfoShape = SampleHold, .lfoSync = Sixteenth,
		.lfoVowel = 2.f},
	Preset{.name = QT_TRANSLATE_NOOP("FormantShifterControls", "Slow Vowel Morph"), .mode = Resonant,
		.voice = Alto, .vowel = 2.f, .lfoShape = Triangle, .lfoSync = Bars4, .lfoVowel = 2.f},
	Preset{.name = QT_TRANSLATE_NOOP("FormantShifterControls", "Throat Singer"), .mode = Resonant,
		.formant = -2.f, .voice = Bass, .vowel = 3.f, .resonance = 3.5f, .drive = 20.f,
		.lfoShape = SmoothRandom, .lfoRate = 0.3f, .lfoFormant = 1.5f},
};

} // namespace


float FormantShifterControls::displayFrequency(int point)
{
	return 20.f * std::pow(1000.f, static_cast<float>(point) / (DisplayPoints - 1));
}


FormantShifterControls::FormantShifterControls(FormantShifterEffect* effect) :
	EffectControls(effect),
	m_effect(effect),
	m_presetModel(this, tr("Preset")),
	m_modeModel(this, tr("Mode")),
	m_formantModel(0.f, -24.f, 24.f, 0.01f, this, tr("Formant")),
	m_pitchModel(0.f, -24.f, 24.f, 1.f, this, tr("Pitch")),
	m_fineModel(0.f, -100.f, 100.f, 1.f, this, tr("Fine tune")),
	m_preserveModel(true, this, tr("Keep formants")),
	m_spreadModel(0.f, 0.f, 12.f, 0.01f, this, tr("Stereo spread")),
	m_voiceModel(this, tr("Voice")),
	m_vowelModel(0.f, 0.f, 4.f, 0.001f, this, tr("Vowel")),
	m_imprintModel(0.f, 0.f, 100.f, 0.1f, this, tr("Imprint")),
	m_resonanceModel(1.f, 0.25f, 4.f, 0.01f, this, tr("Resonance")),
	m_driveModel(0.f, 0.f, 100.f, 0.1f, this, tr("Drive")),
	m_lfoShapeModel(this, tr("LFO shape")),
	m_lfoSyncModel(this, tr("LFO sync")),
	m_lfoRateModel(1.f, 0.02f, 20.f, 0.001f, this, tr("LFO rate")),
	m_lfoVowelModel(0.f, 0.f, 4.f, 0.01f, this, tr("LFO to vowel")),
	m_lfoFormantModel(0.f, 0.f, 12.f, 0.01f, this, tr("LFO to formant")),
	m_envVowelModel(0.f, -4.f, 4.f, 0.01f, this, tr("Envelope to vowel")),
	m_envReleaseModel(150.f, 10.f, 2000.f, 1.f, this, tr("Envelope release")),
	m_mixModel(100.f, 0.f, 100.f, 0.1f, this, tr("Mix")),
	m_gainModel(0.f, -24.f, 24.f, 0.1f, this, tr("Output gain"))
{
	for (const auto& preset : Presets) { m_presetModel.addItem(tr(preset.name)); }
	m_modeModel.addItem(tr("Spectral"));
	m_modeModel.addItem(tr("Resonant"));
	for (const auto& voice : {tr("Bass"), tr("Tenor"), tr("Countertenor"), tr("Alto"), tr("Soprano")})
	{
		m_voiceModel.addItem(voice);
	}
	m_voiceModel.setValue(Tenor);
	for (const auto& shape : {tr("Sine"), tr("Triangle"), tr("Saw up"), tr("Saw down"), tr("Square"),
		tr("Sample & hold"), tr("Smooth random")})
	{
		m_lfoShapeModel.addItem(shape);
	}
	for (const auto& rate : SyncRates) { m_lfoSyncModel.addItem(tr(rate.label)); }

	m_formantModel.setCenterValue(0.f);
	m_pitchModel.setCenterValue(0.f);
	m_fineModel.setCenterValue(0.f);
	m_envVowelModel.setCenterValue(0.f);
	m_gainModel.setCenterValue(0.f);
	m_resonanceModel.setScaleLogarithmic(true);
	m_resonanceModel.setCenterValue(1.f);
	m_lfoRateModel.setScaleLogarithmic(true);
	m_envReleaseModel.setScaleLogarithmic(true);
	m_vowelModel.setStrictStepSize(false);

	connect(&m_presetModel, &Model::dataChanged, this, [this] {
		if (!m_loadingPreset) { applyPreset(m_presetModel.value()); }
	});

	for (auto& point : m_inputEnvelope) { point.store(-120.f, std::memory_order_relaxed); }
	for (auto& point : m_outputEnvelope) { point.store(-120.f, std::memory_order_relaxed); }
}


float FormantShifterControls::lfoSyncTicks() const
{
	const int index = std::clamp(m_lfoSyncModel.value(), 0, static_cast<int>(SyncRates.size()) - 1);
	return SyncRates[index].ticks;
}


void FormantShifterControls::applyPreset(int index)
{
	if (index < 0 || index >= static_cast<int>(Presets.size())) { return; }
	const auto& p = Presets[index];
	m_modeModel.setValue(p.mode);
	m_formantModel.setValue(p.formant);
	m_pitchModel.setValue(p.pitch);
	m_fineModel.setValue(p.fine);
	m_preserveModel.setValue(p.preserve);
	m_spreadModel.setValue(p.spread);
	m_voiceModel.setValue(p.voice);
	m_vowelModel.setValue(p.vowel);
	m_imprintModel.setValue(p.imprint);
	m_resonanceModel.setValue(p.resonance);
	m_driveModel.setValue(p.drive);
	m_lfoShapeModel.setValue(p.lfoShape);
	m_lfoSyncModel.setValue(p.lfoSync);
	m_lfoRateModel.setValue(p.lfoRate);
	m_lfoVowelModel.setValue(p.lfoVowel);
	m_lfoFormantModel.setValue(p.lfoFormant);
	m_envVowelModel.setValue(p.envVowel);
	m_envReleaseModel.setValue(p.envRelease);
	m_mixModel.setValue(p.mix);
	m_gainModel.setValue(p.gain);
}


void FormantShifterControls::loadSettings(const QDomElement& parent)
{
	// The preset index is only a label here: the saved values below are what the project sounds like
	m_loadingPreset = true;
	m_presetModel.loadSettings(parent, "preset");
	m_loadingPreset = false;
	m_modeModel.loadSettings(parent, "mode");
	m_formantModel.loadSettings(parent, "formant");
	m_pitchModel.loadSettings(parent, "pitch");
	m_fineModel.loadSettings(parent, "fine");
	m_preserveModel.loadSettings(parent, "preserve");
	m_spreadModel.loadSettings(parent, "spread");
	m_voiceModel.loadSettings(parent, "voice");
	m_vowelModel.loadSettings(parent, "vowel");
	m_imprintModel.loadSettings(parent, "imprint");
	m_resonanceModel.loadSettings(parent, "res");
	m_driveModel.loadSettings(parent, "drive");
	m_lfoShapeModel.loadSettings(parent, "lfo_shape");
	m_lfoSyncModel.loadSettings(parent, "lfo_sync");
	m_lfoRateModel.loadSettings(parent, "lfo_rate");
	m_lfoVowelModel.loadSettings(parent, "lfo_vowel");
	m_lfoFormantModel.loadSettings(parent, "lfo_formant");
	m_envVowelModel.loadSettings(parent, "env_vowel");
	m_envReleaseModel.loadSettings(parent, "env_release");
	m_mixModel.loadSettings(parent, "mix");
	m_gainModel.loadSettings(parent, "gain");
}


void FormantShifterControls::saveSettings(QDomDocument& doc, QDomElement& parent)
{
	m_presetModel.saveSettings(doc, parent, "preset");
	m_modeModel.saveSettings(doc, parent, "mode");
	m_formantModel.saveSettings(doc, parent, "formant");
	m_pitchModel.saveSettings(doc, parent, "pitch");
	m_fineModel.saveSettings(doc, parent, "fine");
	m_preserveModel.saveSettings(doc, parent, "preserve");
	m_spreadModel.saveSettings(doc, parent, "spread");
	m_voiceModel.saveSettings(doc, parent, "voice");
	m_vowelModel.saveSettings(doc, parent, "vowel");
	m_imprintModel.saveSettings(doc, parent, "imprint");
	m_resonanceModel.saveSettings(doc, parent, "res");
	m_driveModel.saveSettings(doc, parent, "drive");
	m_lfoShapeModel.saveSettings(doc, parent, "lfo_shape");
	m_lfoSyncModel.saveSettings(doc, parent, "lfo_sync");
	m_lfoRateModel.saveSettings(doc, parent, "lfo_rate");
	m_lfoVowelModel.saveSettings(doc, parent, "lfo_vowel");
	m_lfoFormantModel.saveSettings(doc, parent, "lfo_formant");
	m_envVowelModel.saveSettings(doc, parent, "env_vowel");
	m_envReleaseModel.saveSettings(doc, parent, "env_release");
	m_mixModel.saveSettings(doc, parent, "mix");
	m_gainModel.saveSettings(doc, parent, "gain");
}

} // namespace lmms
