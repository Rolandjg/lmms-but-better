/*
 * PumperControls.h - controls for the tempo-synced volume shaper
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

#ifndef LMMS_PUMPER_CONTROLS_H
#define LMMS_PUMPER_CONTROLS_H

#include <atomic>

#include "ComboBoxModel.h"
#include "EffectControls.h"
#include "PumperControlDialog.h"

namespace lmms
{

class PumperEffect;

class PumperControls : public EffectControls
{
	Q_OBJECT
public:
	enum class Band { Full, Lows, Highs };

	PumperControls(PumperEffect* effect);
	~PumperControls() override = default;

	void saveSettings(QDomDocument& doc, QDomElement& parent) override;
	void loadSettings(const QDomElement& parent) override;
	inline QString nodeName() const override { return "PumperControls"; }
	gui::EffectControlDialog* createView() override { return new gui::PumperControlDialog(this); }
	int controlCount() override { return 10; }

	//! Length of one pump cycle in ticks for the selected rate
	float periodTicks() const;

	//! Pick the preset button matching the current length, or "Free"
	void syncPreset();

	//! Gain (0..1) of the volume shape at @p phase (0..1), using the current settings
	float shapeAt(float phase, float periodSeconds) const;

	float m_outPeakL = 0.f;
	float m_outPeakR = 0.f;
	std::atomic<float> m_phase{0.f};   //!< written by the audio thread for the display
	std::atomic<float> m_gain{1.f};

private:
	PumperEffect* m_effect;
	ComboBoxModel m_rateModel;  //!< preset shortcuts for m_lengthModel
	FloatModel m_lengthModel;   //!< cycle length in beats, automatable
	bool m_syncing = false;
	FloatModel m_depthModel;
	FloatModel m_attackModel;
	FloatModel m_holdModel;
	FloatModel m_releaseModel;
	FloatModel m_curveModel;
	FloatModel m_offsetModel;
	ComboBoxModel m_bandModel;
	FloatModel m_crossoverModel;

	friend class gui::PumperControlDialog;
	friend class gui::PumperShapeDisplay;
	friend class PumperEffect;
};

} // namespace lmms

#endif // LMMS_PUMPER_CONTROLS_H
