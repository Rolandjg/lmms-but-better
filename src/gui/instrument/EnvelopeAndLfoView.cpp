/*
 * EnvelopeAndLfoView.cpp - widget which is m_used by envelope/lfo/filter-
 *                          tab of instrument track window
 *
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "EnvelopeAndLfoView.h"

#include <cmath>

#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QSizePolicy>
#include <QVBoxLayout>

#include "EnvelopeGraph.h"
#include "LfoGraph.h"
#include "EnvelopeAndLfoParameters.h"
#include "Knob.h"
#include "DataFile.h"
#include "embed.h"
#include "ModernWidgets.h"
#include "StringPairDrag.h"
#include "TempoSyncKnob.h"
#include "TextFloat.h"
#include "Track.h"

namespace lmms
{

namespace gui
{

namespace
{

//! Small glyphs for the LFO shape selector, in the order of EnvelopeAndLfoParameters::LfoShape
void paintLfoShape(QPainter& p, const QRectF& r, int shape, const QColor& color)
{
	using Shape = EnvelopeAndLfoParameters::LfoShape;
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(QPen(color, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	p.setBrush(Qt::NoBrush);

	const qreal w = std::min(r.width(), 18.0);
	const qreal l = r.center().x() - w / 2, t = r.top() + 1, h = r.height() - 2, b = t + h, m = t + h / 2;
	QPainterPath path;
	switch (static_cast<Shape>(shape))
	{
		case Shape::SineWave:
			path.moveTo(l, m);
			for (int i = 1; i <= 24; ++i)
			{
				path.lineTo(l + w * i / 24, m - std::sin(i / 24.0 * 2 * 3.14159265) * h / 2);
			}
			break;
		case Shape::TriangleWave:
			path.moveTo(l, m);
			path.lineTo(l + w / 4, t);
			path.lineTo(l + w * 3 / 4, b);
			path.lineTo(l + w, m);
			break;
		case Shape::SawWave:
			path.moveTo(l, b);
			path.lineTo(l + w / 2, t);
			path.lineTo(l + w / 2, b);
			path.lineTo(l + w, t);
			break;
		case Shape::SquareWave:
			path.moveTo(l, b);
			path.lineTo(l, t);
			path.lineTo(l + w / 2, t);
			path.lineTo(l + w / 2, b);
			path.lineTo(l + w, b);
			path.lineTo(l + w, t);
			break;
		case Shape::UserDefinedWave:
			// A free-hand squiggle
			path.moveTo(l, m);
			path.cubicTo(l + w * 0.2, t - 1, l + w * 0.3, b, l + w * 0.5, m);
			path.cubicTo(l + w * 0.65, t + h * 0.2, l + w * 0.8, t, l + w, b - h * 0.2);
			break;
		case Shape::RandomWave:
		default:
		{
			// Sample & hold steps
			constexpr qreal levels[] = {0.6, 0.15, 0.85, 0.35, 0.7};
			path.moveTo(l, t + h * levels[0]);
			for (int i = 0; i < 5; ++i)
			{
				path.lineTo(l + w * (i + 1) / 5, t + h * levels[i]);
				if (i < 4) { path.lineTo(l + w * (i + 1) / 5, t + h * levels[i + 1]); }
			}
			break;
		}
	}
	p.drawPath(path);
}

} // namespace

EnvelopeAndLfoView::EnvelopeAndLfoView(QWidget * parent) :
	QWidget(parent),
	ModelView(nullptr, this),
	m_params(nullptr)
{
	// Helper lambda for consistent repeated building of knobs
	auto buildKnob = [&](const QString& label, const QString& hintText, const QString& toolTip)
	{
		auto knob = new Knob(KnobType::Modern, label, this);
		knob->setHintText(hintText, "");
		knob->setToolTip(toolTip);
		return knob;
	};

	QVBoxLayout* mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(0, 0, 0, 0);
	mainLayout->setSpacing(3);

	// Envelope: the editable graph with the knobs below

	m_envelopeGraph = new EnvelopeGraph(this);
	mainLayout->addWidget(m_envelopeGraph, 3);

	QHBoxLayout* envKnobsLayout = new QHBoxLayout();
	envKnobsLayout->setSpacing(0);
	mainLayout->addLayout(envKnobsLayout);

	m_predelayKnob = buildKnob(tr("DEL"), tr("Pre-delay:"), tr("Time before the envelope starts"));
	m_attackKnob = buildKnob(tr("ATT"), tr("Attack:"), tr("Time to rise to the peak"));
	m_holdKnob = buildKnob(tr("HOLD"), tr("Hold:"), tr("Time the envelope stays at the peak"));
	m_decayKnob = buildKnob(tr("DEC"), tr("Decay:"), tr("Time to fall to the sustain level"));
	m_sustainKnob = buildKnob(tr("SUST"), tr("Sustain:"), tr("Level held while the note is down"));
	m_releaseKnob = buildKnob(tr("REL"), tr("Release:"), tr("Time to fade out after the note ends"));
	m_amountKnob = buildKnob(tr("AMT"), tr("Modulation amount:"), tr("How strongly the envelope affects the target"));
	for (auto knob : {m_predelayKnob, m_attackKnob, m_holdKnob, m_decayKnob, m_sustainKnob, m_releaseKnob})
	{
		envKnobsLayout->addWidget(knob);
		envKnobsLayout->addStretch(1);
	}
	envKnobsLayout->addSpacing(6);
	envKnobsLayout->addWidget(m_amountKnob);


	mainLayout->addSpacing(5);


	// LFO: graph and shape selector on the left, knobs and options on the right

	QHBoxLayout* lfoLayout = new QHBoxLayout();
	lfoLayout->setSpacing(6);
	mainLayout->addLayout(lfoLayout, 2);

	QVBoxLayout* graphAndTypesLayout = new QVBoxLayout();
	graphAndTypesLayout->setSpacing(3);
	lfoLayout->addLayout(graphAndTypesLayout, 1);

	m_lfoGraph = new LfoGraph(this);
	graphAndTypesLayout->addWidget(m_lfoGraph, 1);

	m_lfoWaveSelector = new ModernSegmented(this);
	m_lfoWaveSelector->setLabels({tr("Sine"), tr("Triangle"), tr("Saw"), tr("Square"), tr("User"), tr("Random")});
	m_lfoWaveSelector->setToolTips({tr("Sine wave"), tr("Triangle wave"), tr("Saw wave"), tr("Square wave"),
		tr("User-defined wave: drop a sample onto this tab"), tr("Random (sample & hold)")});
	m_lfoWaveSelector->setItemPainter(paintLfoShape, 18);
	graphAndTypesLayout->addWidget(m_lfoWaveSelector);

	QVBoxLayout* knobsAndCheckBoxesLayout = new QVBoxLayout();
	knobsAndCheckBoxesLayout->setSpacing(3);
	lfoLayout->addLayout(knobsAndCheckBoxesLayout);

	QHBoxLayout* lfoKnobsLayout = new QHBoxLayout();
	lfoKnobsLayout->setSpacing(0);
	knobsAndCheckBoxesLayout->addLayout(lfoKnobsLayout);

	m_lfoPredelayKnob = buildKnob(tr("DEL"), tr("Pre-delay:"), tr("Time before the LFO starts"));
	m_lfoAttackKnob = buildKnob(tr("ATT"), tr("Attack:"), tr("Time for the LFO to fade in"));
	m_lfoSpeedKnob = new TempoSyncKnob(KnobType::Modern, tr("SPD"), this);
	m_lfoSpeedKnob->setHintText(tr("Frequency:"), "");
	m_lfoSpeedKnob->setToolTip(tr("LFO speed (right-click to sync to tempo)"));
	m_lfoAmountKnob = buildKnob(tr("AMT"), tr("Modulation amount:"), tr("How strongly the LFO affects the target"));
	for (auto knob : {m_lfoPredelayKnob, m_lfoAttackKnob, static_cast<Knob*>(m_lfoSpeedKnob), m_lfoAmountKnob})
	{
		lfoKnobsLayout->addWidget(knob);
	}
	knobsAndCheckBoxesLayout->addStretch(1);

	QHBoxLayout* checkBoxesLayout = new QHBoxLayout();
	checkBoxesLayout->setSpacing(3);
	knobsAndCheckBoxesLayout->addLayout(checkBoxesLayout);

	m_x100Cb = new ModernToggle(tr("×100"), this);
	m_x100Cb->setToolTip(tr("Multiply LFO frequency by 100"));
	checkBoxesLayout->addWidget(m_x100Cb, 1);

	m_controlEnvAmountCb = new ModernToggle(tr("MOD ENV"), this);
	m_controlEnvAmountCb->setToolTip(tr("Control envelope amount by this LFO"));
	checkBoxesLayout->addWidget(m_controlEnvAmountCb, 1);

	setAcceptDrops(true);
}




EnvelopeAndLfoView::~EnvelopeAndLfoView() = default;




void EnvelopeAndLfoView::modelChanged()
{
	if (m_params) { m_params->getLfoWaveModel().disconnect(this); }
	m_params = castModel<EnvelopeAndLfoParameters>();
	m_envelopeGraph->setModel(m_params);
	m_predelayKnob->setModel( &m_params->m_predelayModel );
	m_attackKnob->setModel( &m_params->m_attackModel );
	m_holdKnob->setModel( &m_params->m_holdModel );
	m_decayKnob->setModel( &m_params->m_decayModel );
	m_sustainKnob->setModel( &m_params->m_sustainModel );
	m_releaseKnob->setModel( &m_params->m_releaseModel );
	m_amountKnob->setModel( &m_params->m_amountModel );

	m_lfoGraph->setModel(m_params);
	m_lfoPredelayKnob->setModel( &m_params->m_lfoPredelayModel );
	m_lfoAttackKnob->setModel( &m_params->m_lfoAttackModel );
	m_lfoSpeedKnob->setModel( &m_params->m_lfoSpeedModel );
	m_lfoAmountKnob->setModel( &m_params->m_lfoAmountModel );
	m_lfoWaveSelector->setModel( &m_params->m_lfoWaveModel );
	m_x100Cb->setModel( &m_params->m_x100Model );
	m_controlEnvAmountCb->setModel( &m_params->m_controlEnvAmountModel );

	connect(&m_params->m_lfoWaveModel, &IntModel::dataChanged, this, &EnvelopeAndLfoView::lfoUserWaveChanged);
}




void EnvelopeAndLfoView::dragEnterEvent( QDragEnterEvent * _dee )
{
	StringPairDrag::processDragEnterEvent( _dee,
					QString( "samplefile,clip_%1" ).arg(
							static_cast<int>(Track::Type::Sample) ) );
}




void EnvelopeAndLfoView::dropEvent( QDropEvent * _de )
{
	QString type = StringPairDrag::decodeKey( _de );
	QString value = StringPairDrag::decodeValue( _de );
	if( type == "samplefile" )
	{
		m_params->m_userWave = SampleBuffer::fromFile(value);
		m_params->m_lfoWaveModel.setValue(static_cast<int>(EnvelopeAndLfoParameters::LfoShape::UserDefinedWave));
		_de->accept();
		update();
	}
	else if( type == QString( "clip_%1" ).arg( static_cast<int>(Track::Type::Sample) ) )
	{
		DataFile dataFile( value.toUtf8() );
		auto file = dataFile.content().
					firstChildElement().firstChildElement().
					firstChildElement().attribute("src");
		m_params->m_userWave = SampleBuffer::fromFile(file);
		m_params->m_lfoWaveModel.setValue(static_cast<int>(EnvelopeAndLfoParameters::LfoShape::UserDefinedWave));
		_de->accept();
		update();
	}
}




void EnvelopeAndLfoView::lfoUserWaveChanged()
{
	// Only hint when the user picks the shape, not when a project or preset loads
	if (!isVisible()) { return; }
	if( static_cast<EnvelopeAndLfoParameters::LfoShape>(m_params->m_lfoWaveModel.value()) ==
				EnvelopeAndLfoParameters::LfoShape::UserDefinedWave )
	{
		if (m_params->m_userWave->size() <= 1)
		{
			TextFloat::displayMessage( tr( "Hint" ),
				tr( "Drag and drop a sample into this window." ),
					embed::getIconPixmap( "hint" ), 3000 );
		}
	}
}

} // namespace gui

} // namespace lmms
