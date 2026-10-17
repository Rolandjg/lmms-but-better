/*
 * LoudnessMeterControls.cpp - settings and readouts of the loudness meter
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

#include "LoudnessMeterControls.h"

#include <algorithm>

#include "LoudnessAnalyzer.h"
#include "LoudnessMeter.h"

namespace lmms
{

namespace
{

struct Target { const char* label; float lufs; float ceiling; };
constexpr std::array<Target, 5> Targets{{
	{"Streaming -14", -14.f, -1.f},
	{"Apple -16", -16.f, -1.f},
	{"Broadcast -23", -23.f, -1.f},
	{"Club -9", -9.f, -0.3f},
	{"Podcast -18", -18.f, -1.f},
}};

}


LoudnessMeterControls::LoudnessMeterControls(LoudnessMeterEffect* effect) :
	EffectControls(effect),
	m_momentary(LoudnessAnalyzer::Silence),
	m_shortTerm(LoudnessAnalyzer::Silence),
	m_integrated(LoudnessAnalyzer::Silence),
	m_effect(effect),
	m_targetModel(this, tr("Target")),
	m_resetOnPlayModel(true, this, tr("Reset when playback starts"))
{
	for (const auto& target : Targets) { m_targetModel.addItem(target.label); }
	for (auto& v : m_momentaryHistory) { v.store(LoudnessAnalyzer::Silence, std::memory_order_relaxed); }
	for (auto& v : m_shortTermHistory) { v.store(LoudnessAnalyzer::Silence, std::memory_order_relaxed); }
}


float LoudnessMeterControls::targetLufs() const
{
	return Targets[std::clamp(m_targetModel.value(), 0, static_cast<int>(Targets.size()) - 1)].lufs;
}


float LoudnessMeterControls::ceilingDb() const
{
	return Targets[std::clamp(m_targetModel.value(), 0, static_cast<int>(Targets.size()) - 1)].ceiling;
}


void LoudnessMeterControls::loadSettings(const QDomElement& parent)
{
	m_targetModel.loadSettings(parent, "target");
	m_resetOnPlayModel.loadSettings(parent, "resetOnPlay");
}


void LoudnessMeterControls::saveSettings(QDomDocument& doc, QDomElement& parent)
{
	m_targetModel.saveSettings(doc, parent, "target");
	m_resetOnPlayModel.saveSettings(doc, parent, "resetOnPlay");
}

} // namespace lmms
