/*
 * LimiterControls.cpp - controls for the lookahead brickwall limiter
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

#include "LimiterControls.h"

#include "Limiter.h"

namespace lmms
{

LimiterControls::LimiterControls(LimiterEffect* effect) :
	EffectControls(effect),
	m_effect(effect),
	m_gainModel(0.f, 0.f, 24.f, 0.1f, this, tr("Gain")),
	m_ceilingModel(-1.f, -12.f, 0.f, 0.1f, this, tr("Ceiling")),
	m_releaseModel(100.f, 1.f, 1000.f, 0.1f, this, tr("Release")),
	m_lookaheadModel(2.f, 0.1f, LimiterDsp::MaxLookaheadMs, 0.1f, this, tr("Lookahead")),
	m_linkModel(100.f, 0.f, 100.f, 1.f, this, tr("Stereo link")),
	m_truePeakModel(true, this, tr("True peak"))
{
	m_releaseModel.setScaleLogarithmic(true);
	m_lookaheadModel.setScaleLogarithmic(true);
	for (auto& g : m_historyGain) { g.store(1.f, std::memory_order_relaxed); }
}


void LimiterControls::loadSettings(const QDomElement& parent)
{
	m_gainModel.loadSettings(parent, "gain");
	m_ceilingModel.loadSettings(parent, "ceiling");
	m_releaseModel.loadSettings(parent, "release");
	m_lookaheadModel.loadSettings(parent, "lookahead");
	m_linkModel.loadSettings(parent, "link");
	m_truePeakModel.loadSettings(parent, "truepeak");
}


void LimiterControls::saveSettings(QDomDocument& doc, QDomElement& parent)
{
	m_gainModel.saveSettings(doc, parent, "gain");
	m_ceilingModel.saveSettings(doc, parent, "ceiling");
	m_releaseModel.saveSettings(doc, parent, "release");
	m_lookaheadModel.saveSettings(doc, parent, "lookahead");
	m_linkModel.saveSettings(doc, parent, "link");
	m_truePeakModel.saveSettings(doc, parent, "truepeak");
}

} // namespace lmms
