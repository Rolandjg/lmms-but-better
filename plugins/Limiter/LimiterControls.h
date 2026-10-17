/*
 * LimiterControls.h - controls for the lookahead brickwall limiter
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

#ifndef LMMS_LIMITER_CONTROLS_H
#define LMMS_LIMITER_CONTROLS_H

#include <array>
#include <atomic>

#include "EffectControls.h"
#include "LimiterControlDialog.h"

namespace lmms
{

class LimiterEffect;

class LimiterControls : public EffectControls
{
	Q_OBJECT
public:
	LimiterControls(LimiterEffect* effect);
	~LimiterControls() override = default;

	void saveSettings(QDomDocument& doc, QDomElement& parent) override;
	void loadSettings(const QDomElement& parent) override;
	inline QString nodeName() const override { return "LimiterControls"; }
	gui::EffectControlDialog* createView() override { return new gui::LimiterControlDialog(this); }
	int controlCount() override { return 6; }

	//! Peaks of the last period, for the output meter
	float m_outPeakL = 0.f;
	float m_outPeakR = 0.f;
	//! Gain the limiter applied at the end of the last period (linear)
	std::atomic<float> m_gain{1.f};

	//! Scrolling history: one column every HistoryStep seconds with the input and output peak and the lowest gain
	static constexpr std::size_t HistorySize = 512;
	static constexpr float HistoryStep = 0.01f;
	std::array<std::atomic<float>, HistorySize> m_historyIn{};
	std::array<std::atomic<float>, HistorySize> m_historyOut{};
	std::array<std::atomic<float>, HistorySize> m_historyGain{};
	std::atomic<std::size_t> m_historyWrite{0};

private:
	LimiterEffect* m_effect;
	FloatModel m_gainModel;
	FloatModel m_ceilingModel;
	FloatModel m_releaseModel;
	FloatModel m_lookaheadModel;
	FloatModel m_linkModel;
	BoolModel m_truePeakModel;

	friend class gui::LimiterControlDialog;
	friend class gui::LimiterDisplay;
	friend class LimiterEffect;
};

} // namespace lmms

#endif // LMMS_LIMITER_CONTROLS_H
