/*
 * CPULoadWidget.cpp - widget for displaying CPU-load (partly based on
 *                      Hydrogen's CPU-load-widget)
 *
 * Copyright (c) 2005-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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


#include <algorithm>
#include <cmath>
#include <QPainter>

#include "AudioEngine.h"
#include "CPULoadWidget.h"
#include "DeprecationHelper.h"
#include "Engine.h"
#include "FontHelper.h"


namespace lmms::gui
{

namespace
{
	// Footprint of the old cpuload_bg.png artwork so surrounding toolbar
	// layouts are unaffected
	constexpr int WidgetWidth = 112;
	constexpr int WidgetHeight = 11;
	//! Where the meter trough starts; the "CPU" label lives left of it
	constexpr int TroughX = 21;
}


CPULoadWidget::CPULoadWidget( QWidget * _parent ) :
	QWidget( _parent ),
	m_currentLoad( 0 ),
	m_updateTimer()
{
	setFixedSize(WidgetWidth, WidgetHeight);

	connect( &m_updateTimer, SIGNAL(timeout()),
					this, SLOT(updateCpuLoad()));
	m_updateTimer.start( 100 );	// update cpu-load at 10 fps
}




void CPULoadWidget::paintEvent( QPaintEvent *  )
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);

	// Label
	p.setFont(adjustedToPixelSize(font(), SMALL_FONT_SIZE));
	p.setPen(m_textColor);
	p.drawText(QRect(0, 0, TroughX - 2, height()), Qt::AlignLeft | Qt::AlignVCenter, "CPU");

	// Meter trough
	const QRectF trough(TroughX, 1.5f, width() - TroughX - 1.f, height() - 3.f);
	p.setPen(Qt::NoPen);
	p.setBrush(m_troughColor);
	p.drawRoundedRect(trough, 2, 2);

	// Fill amount, quantized to full steps. Normally the load indicator moves
	// smoothly with 1 pixel steps but themes can request discrete blocks (like
	// LEDs) via the stepSize property.
	const QRectF fillArea = trough.adjusted(1, 1, -1, -1);
	const float w = std::floor(fillArea.width() * std::min(m_currentLoad, 100) / (stepSize() * 100.f)) * stepSize();
	if (w <= 0) { return; }

	// The load gradient always spans the whole meter so the fill's leading
	// edge takes the color matching the current load.
	QLinearGradient grad(fillArea.left(), 0, fillArea.right(), 0);
	grad.setColorAt(0, m_loadOkColor);
	grad.setColorAt(0.5, m_loadWarnColor);
	grad.setColorAt(1, m_loadClipColor);
	p.setBrush(grad);

	if (stepSize() > 1)
	{
		// Discrete blocks with hairline gaps
		for (float x = 0; x < w; x += stepSize())
		{
			p.drawRect(QRectF(fillArea.left() + x, fillArea.top(), stepSize() - 1.f, fillArea.height()));
		}
	}
	else
	{
		p.drawRoundedRect(QRectF(fillArea.left(), fillArea.top(), w, fillArea.height()), 1, 1);
	}
}




void CPULoadWidget::updateCpuLoad()
{
	// Additional display smoothing for the main load-value. Stronger averaging
	// cannot be used directly in the profiler: cpuLoad() must react fast enough
	// to be useful as overload indicator in AudioEngine::criticalXRuns().
	const int new_load = (m_currentLoad + Engine::audioEngine()->cpuLoad()) / 2;

	if (new_load != m_currentLoad)
	{
		auto engine = Engine::audioEngine();
		setToolTip(
			tr("DSP total: %1%").arg(new_load) + "\n"
			+ tr(" - Notes and setup: %1%").arg(engine->detailLoad(AudioEngineProfiler::DetailType::NoteSetup)) + "\n"
			+ tr(" - Instruments: %1%").arg(engine->detailLoad(AudioEngineProfiler::DetailType::Instruments)) + "\n"
			+ tr(" - Effects: %1%").arg(engine->detailLoad(AudioEngineProfiler::DetailType::Effects)) + "\n"
			+ tr(" - Mixing: %1%").arg(engine->detailLoad(AudioEngineProfiler::DetailType::Mixing))
		);
		m_currentLoad = new_load;
		update();
	}
}


} // namespace lmms::gui
