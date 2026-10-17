/*
 * PumperControls.cpp - controls for the tempo-synced volume shaper
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

#include "PumperControls.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "Pumper.h"

namespace lmms
{

namespace
{

struct Rate { const char* label; float ticks; };
constexpr std::array<Rate, 8> Rates{{
	{"1/1", 192.f}, {"1/2", 96.f}, {"1/4", 48.f}, {"1/8", 24.f}, {"1/16", 12.f},
	{"1/2T", 64.f}, {"1/4T", 32.f}, {"1/8T", 16.f}
}};

}


PumperControls::PumperControls(PumperEffect* effect) :
	EffectControls(effect),
	m_effect(effect),
	m_rateModel(this, tr("Rate")),
	m_depthModel(80.f, 0.f, 100.f, 0.1f, this, tr("Depth")),
	m_attackModel(3.f, 0.1f, 100.f, 0.1f, this, tr("Attack")),
	m_holdModel(0.f, 0.f, 50.f, 0.1f, this, tr("Hold")),
	m_releaseModel(55.f, 1.f, 100.f, 0.1f, this, tr("Release")),
	m_curveModel(35.f, -100.f, 100.f, 0.1f, this, tr("Curve")),
	m_offsetModel(0.f, -50.f, 50.f, 0.1f, this, tr("Offset")),
	m_bandModel(this, tr("Band")),
	m_crossoverModel(150.f, 40.f, 2000.f, 1.f, this, tr("Crossover"))
{
	for (const auto& rate : Rates) { m_rateModel.addItem(rate.label); }
	m_rateModel.setValue(2); // quarter notes: four on the floor
	m_bandModel.addItem(tr("Full"));
	m_bandModel.addItem(tr("Lows"));
	m_bandModel.addItem(tr("Highs"));
	m_crossoverModel.setScaleLogarithmic(true);
	m_attackModel.setScaleLogarithmic(true);
	m_curveModel.setCenterValue(0.f);
	m_offsetModel.setCenterValue(0.f);
}


float PumperControls::periodTicks() const
{
	const int index = std::clamp(m_rateModel.value(), 0, static_cast<int>(Rates.size()) - 1);
	return Rates[index].ticks;
}


float PumperControls::shapeAt(float phase, float periodSeconds) const
{
	const float depth = m_depthModel.value() * 0.01f;
	float attack = periodSeconds > 0.f ? m_attackModel.value() * 0.001f / periodSeconds : 0.f;
	attack = std::clamp(attack, 0.f, 0.5f);
	float hold = m_holdModel.value() * 0.01f;
	float release = m_releaseModel.value() * 0.01f;
	// Squeeze hold and release so the whole shape fits into one cycle
	const float room = 1.f - attack;
	if (hold + release > room)
	{
		const float scale = room / (hold + release);
		hold *= scale;
		release *= scale;
	}

	float duck = 0.f;
	if (phase < attack)
	{
		duck = phase / attack;
	}
	else if (phase < attack + hold)
	{
		duck = 1.f;
	}
	else if (phase < attack + hold + release)
	{
		const float t = (phase - attack - hold) / release;
		// Positive curve stays down longer and snaps back, negative recovers fast and eases in
		const float c = m_curveModel.value() * 0.01f;
		const float k = 1.f + std::abs(c) * 4.f;
		const float shaped = c >= 0.f ? std::pow(t, k) : 1.f - std::pow(1.f - t, k);
		duck = 1.f - shaped;
	}
	return 1.f - depth * duck;
}


void PumperControls::loadSettings(const QDomElement& parent)
{
	m_rateModel.loadSettings(parent, "rate");
	m_depthModel.loadSettings(parent, "depth");
	m_attackModel.loadSettings(parent, "attack");
	m_holdModel.loadSettings(parent, "hold");
	m_releaseModel.loadSettings(parent, "release");
	m_curveModel.loadSettings(parent, "curve");
	m_offsetModel.loadSettings(parent, "offset");
	m_bandModel.loadSettings(parent, "band");
	m_crossoverModel.loadSettings(parent, "crossover");
}


void PumperControls::saveSettings(QDomDocument& doc, QDomElement& parent)
{
	m_rateModel.saveSettings(doc, parent, "rate");
	m_depthModel.saveSettings(doc, parent, "depth");
	m_attackModel.saveSettings(doc, parent, "attack");
	m_holdModel.saveSettings(doc, parent, "hold");
	m_releaseModel.saveSettings(doc, parent, "release");
	m_curveModel.saveSettings(doc, parent, "curve");
	m_offsetModel.saveSettings(doc, parent, "offset");
	m_bandModel.saveSettings(doc, parent, "band");
	m_crossoverModel.saveSettings(doc, parent, "crossover");
}

} // namespace lmms
