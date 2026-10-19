/*
 * FormantShifterControls.h - controls for the formant shifter / vowel filter
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

#ifndef LMMS_FORMANT_SHIFTER_CONTROLS_H
#define LMMS_FORMANT_SHIFTER_CONTROLS_H

#include <array>
#include <atomic>

#include "ComboBoxModel.h"
#include "EffectControls.h"
#include "FormantShifterControlDialog.h"
#include "ModernWidgets.h"

namespace lmms
{

class FormantShifterEffect;

class FormantShifterControls : public EffectControls
{
	Q_OBJECT
public:
	enum class Mode { Spectral, Resonant };

	//! Log-spaced points (20 Hz .. 20 kHz) of the spectral envelopes sent to the display
	static constexpr int DisplayPoints = 192;
	static float displayFrequency(int point);

	FormantShifterControls(FormantShifterEffect* effect);
	~FormantShifterControls() override = default;

	void saveSettings(QDomDocument& doc, QDomElement& parent) override;
	void loadSettings(const QDomElement& parent) override;
	inline QString nodeName() const override { return "FormantShifterControls"; }
	gui::EffectControlDialog* createView() override { return new gui::FormantShifterControlDialog(this); }
	int controlCount() override { return 21; }

	Mode mode() const { return static_cast<Mode>(m_modeModel.value()); }

	//! Length of one LFO cycle in ticks for the selected sync rate, or 0 when free running
	float lfoSyncTicks() const;

	void applyPreset(int index);

	// Written by the audio thread for the editor
	ScopeBuffer m_scope; //!< left = input, right = output (mono sums)
	std::array<std::atomic<float>, DisplayPoints> m_inputEnvelope{};  //!< dBFS
	std::array<std::atomic<float>, DisplayPoints> m_outputEnvelope{}; //!< dBFS
	std::atomic<bool> m_envelopeValid{false};
	std::atomic<float> m_liveVowel{0.f};   //!< modulated vowel position
	std::atomic<float> m_liveFormant{0.f}; //!< modulated formant shift in semitones
	float m_outPeakL = 0.f;
	float m_outPeakR = 0.f;

private:
	FormantShifterEffect* m_effect;

	ComboBoxModel m_presetModel;
	ComboBoxModel m_modeModel;

	FloatModel m_formantModel;
	FloatModel m_pitchModel;
	FloatModel m_fineModel;
	BoolModel m_preserveModel;
	FloatModel m_spreadModel;

	ComboBoxModel m_voiceModel;
	FloatModel m_vowelModel;
	FloatModel m_imprintModel;
	FloatModel m_resonanceModel;
	FloatModel m_driveModel;

	ComboBoxModel m_lfoShapeModel;
	ComboBoxModel m_lfoSyncModel;
	FloatModel m_lfoRateModel;
	FloatModel m_lfoVowelModel;
	FloatModel m_lfoFormantModel;
	FloatModel m_envVowelModel;
	FloatModel m_envReleaseModel;

	FloatModel m_mixModel;
	FloatModel m_gainModel;

	bool m_loadingPreset = false;

	friend class FormantShifterEffect;
	friend class gui::FormantShifterControlDialog;
	friend class gui::FormantDisplay;
	friend class gui::VowelStrip;
};

} // namespace lmms

#endif // LMMS_FORMANT_SHIFTER_CONTROLS_H
