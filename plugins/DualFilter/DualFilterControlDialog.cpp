/*
 * DualFilterControlDialog.cpp - control dialog for dual filter effect
 *
 * Copyright (c) 2014 Vesa Kivimäki <contact/dot/diizy/at/nbl/dot/fi>
 * Copyright (c) 2006-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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


#include "DualFilterControlDialog.h"

#include <QGridLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <complex>
#include <vector>

#include "AudioEngine.h"
#include "BasicFilters.h"
#include "ComboBox.h"
#include "DualFilterControls.h"
#include "Engine.h"
#include "FontHelper.h"
#include "Knob.h"
#include "ModernWidgets.h"

namespace lmms::gui
{

namespace
{

const QColor FilterColors[2] = {modern::accentColor(), modern::secondaryColor()};
constexpr double MinFreq = 20.0;
constexpr double MaxFreq = 20000.0;
constexpr double MinDb = -36.0;
constexpr double MaxDb = 24.0;
constexpr int Points = 160;

//! Complex frequency response of one filter setting, sampled at Points log-spaced frequencies
std::vector<std::complex<double>> measureResponse(int type, float cutoff, float resonance, float sampleRate)
{
	constexpr int length = 4096;
	// A small impulse keeps the nonlinear filter types (Moog, formant) in their linear range
	constexpr float impulse = 0.01f;
	BasicFilters<1> filter(static_cast<sample_rate_t>(sampleRate));
	filter.setFilterType(static_cast<BasicFilters<1>::FilterType>(type));
	filter.calcFilterCoeffs(cutoff, resonance);

	std::vector<float> ir(length);
	for (int n = 0; n < length; ++n)
	{
		ir[n] = filter.update(n == 0 ? impulse : 0.f, 0) / impulse;
	}

	std::vector<std::complex<double>> response(Points);
	for (int i = 0; i < Points; ++i)
	{
		const double freq = MinFreq * std::pow(MaxFreq / MinFreq, static_cast<double>(i) / (Points - 1));
		const auto step = std::polar(1.0, -2.0 * std::numbers::pi * freq / sampleRate);
		std::complex<double> phasor = 1.0, sum = 0.0;
		for (int n = 0; n < length; ++n)
		{
			sum += static_cast<double>(ir[n]) * phasor;
			phasor *= step;
		}
		response[i] = sum;
	}
	return response;
}

} // namespace


DualFilterControlDialog::DualFilterControlDialog( DualFilterControls* controls ) :
	EffectControlDialog( controls )
{
	modern::applyWindowStyle(this);

	auto root = new QVBoxLayout(this);
	root->setContentsMargins(8, 8, 8, 8);
	root->setSpacing(6);
	root->setSizeConstraint(QLayout::SetFixedSize);

	root->addWidget(new FilterResponseDisplay(controls, this));

	auto row = new QHBoxLayout();
	row->setSpacing(6);

	auto makeFilterSection = [&](int index, BoolModel* enabled, ComboBoxModel* type, FloatModel* cut,
		FloatModel* res, FloatModel* gain)
	{
		auto section = new ModernSection(tr("Filter %1").arg(index + 1), this);
		auto toggle = new ModernToggle(tr("ON"), this);
		toggle->setModel(enabled);
		toggle->setToolTip(tr("Enable/disable filter %1").arg(index + 1));
		auto combo = new ComboBox(this);
		combo->setModel(type);
		combo->setMinimumWidth(130);
		section->grid()->addWidget(toggle, 0, 0);
		section->grid()->addWidget(combo, 0, 1, 1, 2);
		section->grid()->addWidget(modern::makeKnob(this, cut, tr("FREQ"), tr("Cutoff frequency:"), " Hz"), 1, 0);
		section->grid()->addWidget(modern::makeKnob(this, res, tr("RESO"), tr("Resonance:"), ""), 1, 1);
		auto gainKnob = new VolumeKnob(KnobType::Modern, tr("GAIN"), this);
		gainKnob->setModel(gain);
		gainKnob->setHintText(tr("Gain:"), "%");
		section->grid()->addWidget(gainKnob, 1, 2);
		return section;
	};

	row->addWidget(makeFilterSection(0, &controls->m_enabled1Model, &controls->m_filter1Model,
		&controls->m_cut1Model, &controls->m_res1Model, &controls->m_gain1Model));

	auto routing = new ModernSection(tr("Routing"), this);
	auto routingSelector = new ModernSegmented(this);
	routingSelector->setModel(&controls->m_routingModel);
	routingSelector->setToolTips({tr("Both filters process the input side by side"),
		tr("Filter 2 processes the output of filter 1")});
	routing->grid()->addWidget(routingSelector, 0, 0, 1, 2);
	routing->grid()->addWidget(modern::makeKnob(this, &controls->m_mixModel, tr("MIX"),
		tr("Mix (filter 1 <-> filter 2):"), ""), 1, 0);
	routing->grid()->addWidget(modern::makeKnob(this, &controls->m_driveModel, tr("DRIVE"),
		tr("Input drive:"), "%"), 1, 1);
	row->addWidget(routing);

	row->addWidget(makeFilterSection(1, &controls->m_enabled2Model, &controls->m_filter2Model,
		&controls->m_cut2Model, &controls->m_res2Model, &controls->m_gain2Model));

	root->addLayout(row);
}




FilterResponseDisplay::FilterResponseDisplay(DualFilterControls* controls, QWidget* parent) :
	QWidget(parent),
	m_controls(controls)
{
	setMinimumSize(sizeHint());
	setMouseTracking(true);
	for (Model* m : std::initializer_list<Model*>{&controls->m_enabled1Model, &controls->m_filter1Model,
		&controls->m_cut1Model, &controls->m_res1Model, &controls->m_gain1Model, &controls->m_mixModel,
		&controls->m_enabled2Model, &controls->m_filter2Model, &controls->m_cut2Model, &controls->m_res2Model,
		&controls->m_gain2Model, &controls->m_routingModel})
	{
		connect(m, &Model::dataChanged, this, qOverload<>(&QWidget::update));
	}
}

double FilterResponseDisplay::freqToX(double freq) const
{
	return 6.0 + (width() - 12.0) * std::log(freq / MinFreq) / std::log(MaxFreq / MinFreq);
}

double FilterResponseDisplay::xToFreq(double x) const
{
	const double norm = std::clamp((x - 6.0) / (width() - 12.0), 0.0, 1.0);
	return MinFreq * std::pow(MaxFreq / MinFreq, norm);
}

QPointF FilterResponseDisplay::handlePosition(int filter) const
{
	const auto& cut = filter == 0 ? m_controls->m_cut1Model : m_controls->m_cut2Model;
	const auto& res = filter == 0 ? m_controls->m_res1Model : m_controls->m_res2Model;
	const double norm = (res.value() - res.minValue()) / (res.maxValue() - res.minValue());
	return QPointF(freqToX(std::max(cut.value(), static_cast<float>(MinFreq))), height() - 14.0 - norm * (height() - 28.0));
}

void FilterResponseDisplay::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	const QRectF area = QRectF(rect());
	modern::paintDisplay(p, area, 0, 0);
	p.setRenderHint(QPainter::Antialiasing);

	const QRectF plot = area.adjusted(6, 6, -6, -6);
	auto toY = [&](double db) { return plot.bottom() - plot.height() * (std::clamp(db, MinDb, MaxDb) - MinDb) / (MaxDb - MinDb); };

	// Grid: decades and 12 dB steps
	p.setPen(QPen(modern::gridColor(), 1));
	for (double f : {100.0, 1000.0, 10000.0}) { p.drawLine(QPointF(freqToX(f), plot.top()), QPointF(freqToX(f), plot.bottom())); }
	for (double db = MinDb + 12; db < MaxDb; db += 12) { p.drawLine(QPointF(plot.left(), toY(db)), QPointF(plot.right(), toY(db))); }
	p.setPen(QPen(QColor(255, 255, 255, 50), 1, Qt::DashLine));
	p.drawLine(QPointF(plot.left(), toY(0)), QPointF(plot.right(), toY(0)));

	const float sr = Engine::audioEngine()->outputSampleRate();
	auto& c = *m_controls;
	const bool enabled[2] = {c.m_enabled1Model.value(), c.m_enabled2Model.value()};
	const float gains[2] = {c.m_gain1Model.value() * 0.01f, c.m_gain2Model.value() * 0.01f};
	const float mix2 = (c.m_mixModel.value() + 1.f) * 0.5f;
	const float mixes[2] = {1.f - mix2, mix2};
	const bool serial = c.m_routingModel.value() == static_cast<int>(DualFilterControls::Routing::Serial);

	std::vector<std::complex<double>> responses[2];
	responses[0] = measureResponse(c.m_filter1Model.value(), c.m_cut1Model.value(), c.m_res1Model.value(), sr);
	responses[1] = measureResponse(c.m_filter2Model.value(), c.m_cut2Model.value(), c.m_res2Model.value(), sr);

	auto pathFor = [&](auto&& valueAt)
	{
		QPainterPath path;
		for (int i = 0; i < Points; ++i)
		{
			const double freq = MinFreq * std::pow(MaxFreq / MinFreq, static_cast<double>(i) / (Points - 1));
			const double db = 20.0 * std::log10(std::max(std::abs(valueAt(i)), 1e-6));
			const QPointF pt(freqToX(freq), toY(db));
			if (i == 0) { path.moveTo(pt); } else { path.lineTo(pt); }
		}
		return path;
	};

	// Individual filters as thin lines
	for (int f = 0; f < 2; ++f)
	{
		if (!enabled[f]) { continue; }
		QColor color = FilterColors[f];
		color.setAlpha(140);
		p.setPen(QPen(color, 1.2, Qt::DashLine));
		p.setBrush(Qt::NoBrush);
		p.drawPath(pathFor([&](int i) { return responses[f][i] * static_cast<double>(gains[f]); }));
	}

	// Combined output, mirroring the processing in DualFilterEffect
	auto combined = [&](int i)
	{
		const std::complex<double> h1 = enabled[0] ? responses[0][i] * static_cast<double>(gains[0]) : 1.0;
		const std::complex<double> h2 = enabled[1] ? responses[1][i] * static_cast<double>(gains[1]) : 1.0;
		if (serial) { return h1 * static_cast<double>(mixes[0]) + h1 * h2 * static_cast<double>(mixes[1]); }
		std::complex<double> sum = 0.0;
		if (enabled[0]) { sum += h1 * static_cast<double>(mixes[0]); }
		if (enabled[1]) { sum += h2 * static_cast<double>(mixes[1]); }
		return sum;
	};
	const QPainterPath total = pathFor(combined);
	QPainterPath fill = total;
	fill.lineTo(plot.right(), plot.bottom());
	fill.lineTo(plot.left(), plot.bottom());
	fill.closeSubpath();
	QLinearGradient grad(0, plot.top(), 0, plot.bottom());
	grad.setColorAt(0, QColor(255, 255, 255, 60));
	grad.setColorAt(1, QColor(255, 255, 255, 5));
	p.setPen(Qt::NoPen);
	p.setBrush(grad);
	p.drawPath(fill);
	p.setPen(QPen(modern::textColor(), 2));
	p.setBrush(Qt::NoBrush);
	p.drawPath(total);

	// Handles
	p.setFont(adjustedToPixelSize(font(), SMALL_FONT_SIZE));
	for (int f = 0; f < 2; ++f)
	{
		const QPointF pos = handlePosition(f);
		QColor color = FilterColors[f];
		if (!enabled[f]) { color = QColor(110, 110, 110); }
		p.setPen(QPen(QColor(10, 12, 14), 1.5));
		p.setBrush(color);
		p.drawEllipse(pos, 7, 7);
		p.setPen(QColor(16, 18, 20));
		p.drawText(QRectF(pos.x() - 7, pos.y() - 7, 14, 14), Qt::AlignCenter, QString::number(f + 1));
	}

	p.setPen(modern::dimTextColor());
	p.drawText(QRectF(plot.left() + 4, plot.top(), 120, 14), Qt::AlignLeft | Qt::AlignVCenter,
		serial ? tr("serial") : tr("parallel"));
}

void FilterResponseDisplay::mousePressEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton) { return; }
	m_dragFilter = -1;
	double best = 14.0;
	for (int f = 0; f < 2; ++f)
	{
		const double distance = QLineF(handlePosition(f), event->pos()).length();
		if (distance < best) { best = distance; m_dragFilter = f; }
	}
	if (m_dragFilter >= 0)
	{
		(m_dragFilter == 0 ? m_controls->m_cut1Model : m_controls->m_cut2Model).addJournalCheckPoint();
		(m_dragFilter == 0 ? m_controls->m_res1Model : m_controls->m_res2Model).addJournalCheckPoint();
	}
}

void FilterResponseDisplay::mouseMoveEvent(QMouseEvent* event)
{
	if (m_dragFilter < 0)
	{
		bool near = false;
		for (int f = 0; f < 2; ++f) { near |= QLineF(handlePosition(f), event->pos()).length() < 14.0; }
		setCursor(near ? Qt::SizeAllCursor : Qt::ArrowCursor);
		return;
	}
	auto& cut = m_dragFilter == 0 ? m_controls->m_cut1Model : m_controls->m_cut2Model;
	auto& res = m_dragFilter == 0 ? m_controls->m_res1Model : m_controls->m_res2Model;
	cut.setValue(static_cast<float>(xToFreq(event->pos().x())));
	const double norm = std::clamp((height() - 14.0 - event->pos().y()) / (height() - 28.0), 0.0, 1.0);
	res.setValue(res.minValue() + static_cast<float>(norm) * (res.maxValue() - res.minValue()));
}

void FilterResponseDisplay::mouseReleaseEvent(QMouseEvent*)
{
	m_dragFilter = -1;
}


} // namespace lmms::gui
