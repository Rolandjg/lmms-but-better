/*
 * ReverbSCControlDialog.cpp - control dialog for ReverbSC
 *
 * Copyright (c) 2017 Paul Batchelor
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


#include "ReverbSCControlDialog.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <cmath>

#include "FontHelper.h"
#include "Knob.h"
#include "ModernWidgets.h"
#include "ReverbSCControls.h"

namespace lmms::gui
{

namespace
{
const QColor Accent = modern::accentColor();

//! Approximate RT60 of the FDN for a per-pass gain; the mean loop delay of revsc is about 45 ms
float rt60(float loopGain)
{
	constexpr float meanLoop = 0.045f;
	loopGain = std::clamp(loopGain, 0.0001f, 0.99999f);
	return std::min(-3.f * meanLoop / std::log10(loopGain), 60.f);
}
}


ReverbSCControlDialog::ReverbSCControlDialog( ReverbSCControls* controls ) :
	EffectControlDialog( controls )
{
	modern::applyWindowStyle(this);

	auto root = new QVBoxLayout(this);
	root->setContentsMargins(8, 8, 8, 8);
	root->setSpacing(6);
	root->setSizeConstraint(QLayout::SetFixedSize);

	auto top = new QHBoxLayout();
	top->setSpacing(6);
	top->addWidget(new ReverbDecayDisplay(controls, this), 1);
	top->addWidget(new ModernMeter(this, &controls->m_outPeakL, &controls->m_outPeakR));
	root->addLayout(top);

	auto row = new QHBoxLayout();
	row->setSpacing(6);

	auto input = new ModernSection(tr("Input"), this);
	input->grid()->addWidget(modern::makeKnob(this, &controls->m_inputGainModel, tr("GAIN"),
		tr("Input gain:"), " dB"), 0, 0);
	input->grid()->addWidget(modern::makeKnob(this, &controls->m_predelayModel, tr("PRE-DLY"),
		tr("Pre-delay:"), " ms"), 0, 1);
	input->grid()->addWidget(modern::makeKnob(this, &controls->m_lowCutModel, tr("LO CUT"),
		tr("Input low cut:"), " Hz"), 0, 2);
	row->addWidget(input);

	auto space = new ModernSection(tr("Space"), this);
	space->grid()->addWidget(modern::makeKnob(this, &controls->m_sizeModel, tr("SIZE"),
		tr("Size (decay):"), ""), 0, 0);
	space->grid()->addWidget(modern::makeKnob(this, &controls->m_colorModel, tr("COLOR"),
		tr("Damping frequency:"), " Hz"), 0, 1);
	space->grid()->addWidget(modern::makeKnob(this, &controls->m_modulationModel, tr("MOD"),
		tr("Modulation depth:"), "%"), 0, 2);
	auto freeze = new ModernToggle(tr("FREEZE"), this);
	freeze->setModel(&controls->m_freezeModel);
	freeze->setToolTip(tr("Sustain the current tail forever and stop accepting input"));
	space->grid()->addWidget(freeze, 1, 0, 1, 3);
	row->addWidget(space);

	auto output = new ModernSection(tr("Output"), this);
	output->grid()->addWidget(modern::makeKnob(this, &controls->m_widthModel, tr("WIDTH"),
		tr("Stereo width:"), "%"), 0, 0);
	output->grid()->addWidget(modern::makeKnob(this, &controls->m_duckModel, tr("DUCK"),
		tr("Duck tail while input plays:"), "%"), 0, 1);
	output->grid()->addWidget(modern::makeKnob(this, &controls->m_outputGainModel, tr("GAIN"),
		tr("Output gain:"), " dB"), 0, 2);
	row->addWidget(output);

	root->addLayout(row);
}




ReverbDecayDisplay::ReverbDecayDisplay(ReverbSCControls* controls, QWidget* parent) :
	QWidget(parent),
	m_controls(controls)
{
	setMinimumSize(sizeHint());
	for (Model* m : std::initializer_list<Model*>{&controls->m_sizeModel, &controls->m_colorModel,
		&controls->m_predelayModel, &controls->m_freezeModel})
	{
		connect(m, &Model::dataChanged, this, qOverload<>(&QWidget::update));
	}
}




void ReverbDecayDisplay::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	const QRectF area = QRectF(rect());
	modern::paintDisplay(p, area, 10, 3);
	p.setRenderHint(QPainter::Antialiasing);

	const bool frozen = m_controls->m_freezeModel.value();
	const float size = m_controls->m_sizeModel.value();
	const float color = m_controls->m_colorModel.value();
	const float predelay = m_controls->m_predelayModel.value() * 0.001f;

	// One-pole damping evaluated at 6 kHz reduces the loop gain for the highs
	const float hfLoss = 1.f / std::sqrt(1.f + std::pow(6000.f / color, 2.f));
	const float rtLow = frozen ? 60.f : rt60(size);
	const float rtHigh = frozen ? 60.f : rt60(size * hfLoss);
	const float window = std::clamp(predelay + std::min(rtLow, 20.f) * 1.1f, 0.5f, 22.f);

	const QRectF plot = area.adjusted(8, 10, -8, -18);
	auto curve = [&](float rt)
	{
		QPainterPath path(QPointF(plot.left(), plot.bottom()));
		const int steps = 120;
		for (int i = 0; i <= steps; ++i)
		{
			const float t = window * i / steps;
			float level = 0.f;
			if (t >= predelay)
			{
				// 60 dB of decay over rt seconds, displayed on a 60 dB scale
				const float db = -60.f * (t - predelay) / rt;
				level = std::max(0.f, 1.f + db / 60.f);
			}
			path.lineTo(plot.left() + plot.width() * i / steps, plot.bottom() - plot.height() * level);
		}
		path.lineTo(plot.right(), plot.bottom());
		path.closeSubpath();
		return path;
	};

	QLinearGradient fill(0, plot.top(), 0, plot.bottom());
	QColor a = Accent;
	a.setAlpha(150);
	fill.setColorAt(0, a);
	a.setAlpha(20);
	fill.setColorAt(1, a);
	p.setPen(QPen(Accent, 1.5));
	p.setBrush(fill);
	p.drawPath(curve(rtLow));

	QColor hi = modern::secondaryColor();
	hi.setAlpha(110);
	p.setPen(QPen(modern::secondaryColor(), 1.5));
	p.setBrush(hi);
	p.drawPath(curve(rtHigh));

	p.setFont(adjustedToPixelSize(font(), SMALL_FONT_SIZE));
	p.setPen(modern::textColor());
	const QString decay = frozen ? tr("Decay: frozen") : tr("Decay ≈ %1 s").arg(rtLow, 0, 'f', rtLow < 10 ? 2 : 1);
	p.drawText(QRectF(area.left() + 10, area.bottom() - 17, 200, 14), Qt::AlignLeft | Qt::AlignVCenter,
		decay + tr("   ·   highs ≈ %1 s").arg(rtHigh, 0, 'f', rtHigh < 10 ? 2 : 1));
	p.setPen(modern::dimTextColor());
	p.drawText(QRectF(area.right() - 110, area.bottom() - 17, 100, 14), Qt::AlignRight | Qt::AlignVCenter,
		tr("%1 s").arg(window, 0, 'f', 1));
}


} // namespace lmms::gui
