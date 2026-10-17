/*
 * LoudnessMeterControls.h - settings and readouts of the loudness meter
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

#ifndef LMMS_LOUDNESS_METER_CONTROLS_H
#define LMMS_LOUDNESS_METER_CONTROLS_H

#include <array>
#include <atomic>

#include "ComboBoxModel.h"
#include "EffectControls.h"
#include "LoudnessMeterDialog.h"

namespace lmms
{

class LoudnessMeterEffect;

class LoudnessMeterControls : public EffectControls
{
	Q_OBJECT
public:
	LoudnessMeterControls(LoudnessMeterEffect* effect);
	~LoudnessMeterControls() override = default;

	void saveSettings(QDomDocument& doc, QDomElement& parent) override;
	void loadSettings(const QDomElement& parent) override;
	inline QString nodeName() const override { return "LoudnessMeterControls"; }
	gui::EffectControlDialog* createView() override { return new gui::LoudnessMeterDialog(this); }
	int controlCount() override { return 2; }

	//! Integrated loudness target of the selected delivery platform, in LUFS
	float targetLufs() const;
	//! True-peak ceiling of the selected delivery platform, in dBTP
	float ceilingDb() const;

	//! Readouts published by the audio thread
	std::atomic<float> m_momentary;
	std::atomic<float> m_shortTerm;
	std::atomic<float> m_integrated;
	std::atomic<float> m_range{0.f};
	std::atomic<float> m_truePeak{0.f};
	std::atomic<bool> m_resetRequested{false};

	//! Momentary and short-term loudness every 100 ms for the history graph
	static constexpr std::size_t HistorySize = 600;
	std::array<std::atomic<float>, HistorySize> m_momentaryHistory;
	std::array<std::atomic<float>, HistorySize> m_shortTermHistory;
	std::atomic<std::size_t> m_historyWrite{0};

private:
	LoudnessMeterEffect* m_effect;
	ComboBoxModel m_targetModel;
	BoolModel m_resetOnPlayModel;

	friend class gui::LoudnessMeterDialog;
	friend class gui::LoudnessDisplay;
	friend class LoudnessMeterEffect;
};

} // namespace lmms

#endif // LMMS_LOUDNESS_METER_CONTROLS_H
