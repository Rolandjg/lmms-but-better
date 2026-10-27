/*
 * FigglebottomTuneControls.h - controls for the FigglebottomTune pitch corrector
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

#ifndef LMMS_FIGGLEBOTTOM_TUNE_CONTROLS_H
#define LMMS_FIGGLEBOTTOM_TUNE_CONTROLS_H

#include <array>
#include <atomic>
#include <cmath>
#include <limits>

#include "ComboBoxModel.h"
#include "EffectControls.h"
#include "FigglebottomTuneControlDialog.h"
#include "FigglebottomTuneDsp.h"

namespace lmms
{

class FigglebottomTuneEffect;

//! Lock-free history of the pitch track, one entry per analysis hop, written by the audio thread
//! and drawn by the editor. Unvoiced hops store NaN pitches.
class PitchTrace
{
public:
	static constexpr std::size_t Size = 4096;

	struct Entry
	{
		float input;
		float output;
		int target;
		float level;
	};

	void push(const figglebottomtune::Correction& c, float levelDb)
	{
		constexpr float None = std::numeric_limits<float>::quiet_NaN();
		const auto count = m_count.load(std::memory_order_relaxed);
		const auto i = count % Size;
		m_input[i].store(c.voiced ? c.input : None, std::memory_order_relaxed);
		m_output[i].store(c.voiced ? c.output : None, std::memory_order_relaxed);
		m_target[i].store(c.voiced ? c.target : -1, std::memory_order_relaxed);
		m_level[i].store(levelDb, std::memory_order_relaxed);
		m_count.store(count + 1, std::memory_order_release);
	}

	//! Entries written so far; the newest is count() - 1
	std::uint64_t count() const { return m_count.load(std::memory_order_acquire); }

	Entry at(std::uint64_t index) const
	{
		const auto i = index % Size;
		return {m_input[i].load(std::memory_order_relaxed), m_output[i].load(std::memory_order_relaxed),
			m_target[i].load(std::memory_order_relaxed), m_level[i].load(std::memory_order_relaxed)};
	}

	void setHopSeconds(float seconds) { m_hopSeconds.store(seconds, std::memory_order_relaxed); }
	float hopSeconds() const { return m_hopSeconds.load(std::memory_order_relaxed); }

private:
	std::array<std::atomic<float>, Size> m_input{};
	std::array<std::atomic<float>, Size> m_output{};
	std::array<std::atomic<int>, Size> m_target{};
	std::array<std::atomic<float>, Size> m_level{};
	std::atomic<std::uint64_t> m_count{0};
	std::atomic<float> m_hopSeconds{0.003f};
};


class FigglebottomTuneControls : public EffectControls
{
	Q_OBJECT
public:
	FigglebottomTuneControls(FigglebottomTuneEffect* effect);
	~FigglebottomTuneControls() override = default;

	void saveSettings(QDomDocument& doc, QDomElement& parent) override;
	void loadSettings(const QDomElement& parent) override;
	inline QString nodeName() const override { return "FigglebottomTuneControls"; }
	gui::EffectControlDialog* createView() override { return new gui::FigglebottomTuneControlDialog(this); }
	int controlCount() override { return 26; }

	void applyPreset(int index);

	bool noteEnabled(int pitchClass) const { return m_noteModels[pitchClass]->value(); }
	void toggleNote(int pitchClass);
	void setNote(int pitchClass, bool on);

	static QString noteName(int note); //!< e.g. "C#4" for MIDI note 61

	// Written by the audio thread for the editor
	PitchTrace m_trace;
	float m_outPeakL = 0.f;
	float m_outPeakR = 0.f;

private:
	void applyScale();
	void updateScaleFromNotes();

	FigglebottomTuneEffect* m_effect;

	ComboBoxModel m_presetModel;
	ComboBoxModel m_keyModel;
	ComboBoxModel m_scaleModel;
	std::array<BoolModel*, figglebottomtune::PitchClasses> m_noteModels{};

	FloatModel m_retuneModel;
	FloatModel m_glideModel;
	FloatModel m_amountModel;
	FloatModel m_humanizeModel;

	FloatModel m_stickyModel;
	FloatModel m_toleranceModel;
	FloatModel m_trackingModel;

	FloatModel m_shiftModel;
	FloatModel m_fineModel;
	FloatModel m_mixModel;
	FloatModel m_gainModel;

	bool m_loadingPreset = false;
	bool m_syncingNotes = false; //!< set while a key or scale change writes the note switches

	friend class FigglebottomTuneEffect;
	friend class gui::FigglebottomTuneControlDialog;
	friend class gui::PitchRoll;
};

} // namespace lmms

#endif // LMMS_FIGGLEBOTTOM_TUNE_CONTROLS_H
