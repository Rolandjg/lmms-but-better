/*
 * StereoEnhancerControls.cpp - control-dialog for StereoEnhancer effect
 *
 * Copyright (c) 2006-2008 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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



#include "StereoEnhancerControls.h"
#include "StereoEnhancer.h"

namespace lmms
{


StereoEnhancerControls::StereoEnhancerControls( StereoEnhancerEffect * _eff ) :
		EffectControls( _eff ),
		m_effect( _eff ),
		m_widthModel(0.0f, 0.0f, 180.0f, 1.0f, this, tr( "Haas width" ) ),
		m_lowWidthModel(100.f, 0.f, 200.f, 0.1f, this, tr("Low band width")),
		m_midWidthModel(100.f, 0.f, 200.f, 0.1f, this, tr("Mid band width")),
		m_highWidthModel(100.f, 0.f, 200.f, 0.1f, this, tr("High band width")),
		m_lowCrossoverModel(200.f, 40.f, 1000.f, 1.f, this, tr("Low/mid crossover")),
		m_highCrossoverModel(3000.f, 1000.f, 12000.f, 1.f, this, tr("Mid/high crossover"))
{
	for (auto model : {&m_lowWidthModel, &m_midWidthModel, &m_highWidthModel}) { model->setCenterValue(100.f); }
	m_lowCrossoverModel.setScaleLogarithmic(true);
	m_highCrossoverModel.setScaleLogarithmic(true);

	connect( &m_widthModel, SIGNAL( dataChanged() ),
			this, SLOT( changeWideCoeff() ) );

	changeWideCoeff();
}



void StereoEnhancerControls::changeWideCoeff()
{
	m_effect->m_seFX.setWideCoeff( m_widthModel.value() );
}



void StereoEnhancerControls::loadSettings( const QDomElement & _this )
{
	m_widthModel.loadSettings( _this, "width" );
	m_lowWidthModel.loadSettings(_this, "lowWidth");
	m_midWidthModel.loadSettings(_this, "midWidth");
	m_highWidthModel.loadSettings(_this, "highWidth");
	m_lowCrossoverModel.loadSettings(_this, "lowXover");
	m_highCrossoverModel.loadSettings(_this, "highXover");
	m_lowCrossoverModel.setScaleLogarithmic(true);
	m_highCrossoverModel.setScaleLogarithmic(true);
}




void StereoEnhancerControls::saveSettings( QDomDocument & _doc, 
							QDomElement & _this )
{
	m_widthModel.saveSettings( _doc, _this, "width" );
	m_lowWidthModel.saveSettings(_doc, _this, "lowWidth");
	m_midWidthModel.saveSettings(_doc, _this, "midWidth");
	m_highWidthModel.saveSettings(_doc, _this, "highWidth");
	m_lowCrossoverModel.saveSettings(_doc, _this, "lowXover");
	m_highCrossoverModel.saveSettings(_doc, _this, "highXover");
}


} // namespace lmms
