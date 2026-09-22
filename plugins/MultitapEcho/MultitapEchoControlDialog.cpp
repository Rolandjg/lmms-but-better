/*
 * MultitapEchoControlDialog.cpp - a multitap echo delay plugin
 *
 * Copyright (c) 2014 Vesa Kivimäki <contact/dot/diizy/at/nbl/dot/fi>
 * Copyright (c) 2008-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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


#include "MultitapEchoControlDialog.h"

#include <QPainter>
#include "MultitapEchoControls.h"
#include "embed.h"
#include "FontHelper.h"
#include "Graph.h"
#include "LedCheckBox.h"
#include "Knob.h"
#include "TempoSyncKnob.h"
#include "LcdSpinBox.h"

namespace lmms::gui
{


MultitapEchoControlDialog::MultitapEchoControlDialog( MultitapEchoControls * controls ) :
	EffectControlDialog( controls )
{
	setFixedSize(245, 300 + PanPanelHeight);
	QPalette pal;
	
	// graph widgets

	auto ampGraph = new Graph(this, Graph::Style::Bar, 204, 105);
	auto lpGraph = new Graph(this, Graph::Style::Bar, 204, 105);
	auto panGraph = new Graph(this, Graph::Style::Bar, 204, 105);
	panGraph->move(30, 240);
	panGraph->setModel(&controls->m_panGraph);
	panGraph->setToolTip(tr("Pan of each tap: bottom = left, middle = center, top = right"));

	ampGraph->move( 30, 10 );
	lpGraph->move( 30, 125 );
	
	ampGraph->setModel( & controls->m_ampGraph );
	lpGraph->setModel( & controls->m_lpGraph );
	
	pal = QPalette();
	pal.setBrush( backgroundRole(),	PLUGIN_NAME::getIconPixmap("graph_bg") );
	
	ampGraph->setAutoFillBackground( true );
	ampGraph->setPalette( pal );
	ampGraph->setGraphColor( QColor( 11, 213, 86) );
	ampGraph -> setMaximumSize( 204, 105 );
	
	lpGraph->setAutoFillBackground( true );
	lpGraph->setPalette( pal );
	lpGraph->setGraphColor( QColor( 0, 200, 187) );
	lpGraph -> setMaximumSize( 204, 105 );

	panGraph->setAutoFillBackground(true);
	panGraph->setPalette(pal);
	panGraph->setGraphColor(QColor(70, 150, 255));
	panGraph->setMaximumSize(204, 105);
	
	// steps spinbox

	auto steps = new LcdSpinBox(2, this, "Steps");
	steps->move( 20, 245 + PanPanelHeight );
	steps->setModel( & controls->m_steps );
	
	// knobs

	auto stepLength = new TempoSyncKnob(KnobType::Bright26, tr("Length"), SMALL_FONT_SIZE,  this);
	stepLength->move( 100, 245 + PanPanelHeight );
	stepLength->setModel( & controls->m_stepLength );
	stepLength->setHintText( tr( "Step length:" ) , " ms" );

	auto dryGain = new Knob(KnobType::Bright26, tr("Dry"), SMALL_FONT_SIZE, this);
	dryGain->move( 150, 245 + PanPanelHeight );
	dryGain->setModel( & controls->m_dryGain );
	dryGain->setHintText( tr( "Dry gain:" ) , " dBFS" );

	auto stages = new Knob(KnobType::Bright26, tr("Stages"), SMALL_FONT_SIZE, this);
	stages->move( 200, 245 + PanPanelHeight );
	stages->setModel( & controls->m_stages );
	stages->setHintText( tr( "Low-pass stages:" ) , "x" );
	// switch led

	auto swapInputs = new LedCheckBox("Swap inputs", this, tr("Swap inputs"), LedCheckBox::LedColor::Green);
	swapInputs->move( 20, 283 + PanPanelHeight );

	auto feedback = new Knob(KnobType::Bright26, tr("Fdbk"), SMALL_FONT_SIZE, this);
	feedback->move(58, 245 + PanPanelHeight);
	feedback->setModel(&controls->m_feedback);
	feedback->setHintText(tr("Feedback:"), "%");
	feedback->setToolTip(tr("Repeat the whole tap pattern"));
	swapInputs->setModel( & controls->m_swapInputs );
	swapInputs->setToolTip(tr("Swap left and right input channels for reflections"));
}


void MultitapEchoControlDialog::paintEvent(QPaintEvent*)
{
	static const auto artwork = PLUGIN_NAME::getIconPixmap("artwork");
	QPainter p(this);
	// Both original graph panels, then a copy of the lower one (without its scale) for panning
	p.drawPixmap(0, 0, artwork, 0, 0, 245, 235);
	p.drawPixmap(0, 235, artwork, 0, 120, 245, PanPanelHeight);
	p.drawPixmap(QRect(4, 237, 24, PanPanelHeight - 4), artwork, QRect(30, 122, 1, PanPanelHeight - 4));
	p.drawPixmap(0, 235 + PanPanelHeight, artwork, 0, 235, 245, 300 - 235);

	// Scale in the style of the printed ones
	p.setFont(adjustedToPixelSize(font(), SMALL_FONT_SIZE));
	p.setPen(QColor(230, 230, 230));
	const struct { const char* text; int y; } marks[] = {{"R", 248}, {"C", 294}, {"L", 340}};
	for (const auto& mark : marks)
	{
		p.drawText(QRect(4, mark.y - 7, 18, 14), Qt::AlignRight | Qt::AlignVCenter, tr(mark.text));
		p.drawLine(23, mark.y, 27, mark.y);
	}
}


} // namespace lmms::gui
