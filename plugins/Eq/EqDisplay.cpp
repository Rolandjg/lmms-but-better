/*
 * EqDisplay.cpp - implementation of the EqDisplay class, the equalizer's curve editor
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

#include "EqDisplay.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include <QAction>
#include <QContextMenuEvent>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QLineF>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include "AudioEngine.h"
#include "ControlAccessibility.h"
#include "Engine.h"
#include "EqControls.h"
#include "FontHelper.h"
#include "GuiApplication.h"
#include "KeyboardShortcuts.h"
#include "MainWindow.h"
#include "ModernWidgets.h"

namespace lmms::gui
{

namespace
{

constexpr float MinFreq = 20.f;
constexpr float MaxFreq = 20000.f;
//! Level, relative to the loudest band, where the spectrum reaches the bottom of the display
constexpr float SpectrumFloorDb = -42.f;
constexpr double NodeRadius = 5.5;
constexpr double GrabRadius = 11.0;

//! Normalised biquad coefficients: a1, a2, b0, b1, b2
using Coeffs = std::array<double, 5>;

Coeffs normalised(double a0, double a1, double a2, double b0, double b1, double b2)
{
	return {a1 / a0, a2 / a0, b0 / a0, b1 / a0, b2 / a0};
}

//! Log magnitude of a normalised biquad, from the Audio EQ Cookbook
double magnitudeDb(const Coeffs& c, double freq, double sampleRate)
{
	const double a1 = c[0], a2 = c[1], b0 = c[2], b1 = c[3], b2 = c[4];
	const double w = std::sin(std::numbers::pi * freq / sampleRate);
	const double phi = 4 * w * w;
	const double bb = b0 + b1 + b2;
	const double aa = 1 + a1 + a2;
	return 10 * std::log10(bb * bb + (b0 * b2 * phi - (b1 * (b0 + b2) + 4 * b0 * b2)) * phi)
		- 10 * std::log10(aa * aa + (a2 * phi - (a1 * (1 + a2) + 4 * a2)) * phi);
}

//! Coefficients of one band, mirroring the filters in EqFilter.h so the curve is what is heard
Coeffs bandCoefficients(const EqBand& band, double sampleRate)
{
	using namespace std::numbers;
	const double freq = std::clamp<double>(band.freq->value(), MinFreq, sampleRate * 0.49);
	const double res = std::max(0.01f, band.q->value());
	const double gainDb = band.hasGain() ? band.gain->value() : 0.0;
	const double w0 = 2 * pi * freq / sampleRate;
	const double c = std::cos(w0);
	const double s = std::sin(w0);
	const double a = std::pow(10.0, gainDb * 0.025);

	switch (band.type)
	{
	case EqBandType::HighPass:
	{
		const double alpha = s / (2 * res);
		return normalised(1 + alpha, -2 * c, 1 - alpha, (1 + c) * 0.5, -(1 + c), (1 + c) * 0.5);
	}
	case EqBandType::LowPass:
	{
		const double alpha = s / (2 * res);
		return normalised(1 + alpha, -2 * c, 1 - alpha, (1 - c) * 0.5, 1 - c, (1 - c) * 0.5);
	}
	case EqBandType::LowShelf:
	{
		const double beta = std::sqrt(a) / res;
		return normalised((a + 1) + (a - 1) * c + beta * s, -2 * ((a - 1) + (a + 1) * c),
			(a + 1) + (a - 1) * c - beta * s, a * ((a + 1) - (a - 1) * c + beta * s),
			2 * a * ((a - 1) - (a + 1) * c), a * ((a + 1) - (a - 1) * c - beta * s));
	}
	case EqBandType::HighShelf:
	{
		const double beta = std::sqrt(a) / res;
		return normalised((a + 1) - (a - 1) * c + beta * s, 2 * ((a - 1) - (a + 1) * c),
			(a + 1) - (a - 1) * c - beta * s, a * ((a + 1) + (a - 1) * c + beta * s),
			-2 * a * ((a - 1) + (a + 1) * c), a * ((a + 1) + (a - 1) * c - beta * s));
	}
	case EqBandType::Peak:
	default:
	{
		// here the resonance model holds a bandwidth in octaves
		const double alpha = s * std::sinh(ln2 / 2 * res * w0 / s);
		return normalised(1 + alpha / a, -2 * c, 1 - alpha / a, 1 + alpha * a, -2 * c, 1 - alpha * a);
	}
	}
}

//! Hue ramp from the theme's accent through its secondary color into blue, so the eight bands tell
//! themselves apart without inventing colors the theme doesn't have
QColor nodeColor(int index)
{
	const QColor accent = modern::accentColor();
	const QColor secondary = modern::secondaryColor();
	if (accent.hue() < 0 || secondary.hue() < 0) { return accent; }

	const float t = index / static_cast<float>(EqDisplay::BandCount - 1);
	const auto mix = [t](int from, int to) { return static_cast<int>(from + (to - from) * t); };
	const int hue = ((accent.hue() + mix(0, (secondary.hue() - accent.hue()) * 2)) % 360 + 360) % 360;
	return QColor::fromHsv(hue, mix(accent.saturation(), secondary.saturation()),
		mix(accent.value(), secondary.value()));
}

QString formatFreq(float freq)
{
	if (freq < 1000.f) { return QString::number(freq, 'f', freq < 100.f ? 1 : 0) + " Hz"; }
	return QString::number(freq / 1000.f, 'f', freq < 10000.f ? 2 : 1) + " kHz";
}

QString formatGain(float gainDb)
{
	return (gainDb >= 0.f ? QString("+") : QString()) + QString::number(gainDb, 'f', 1) + " dB";
}

//! Grid lines of the frequency axis; the ones with a label are drawn a little brighter
struct FreqTick
{
	float freq;
	const char* label;
};

constexpr std::array<FreqTick, 22> FreqTicks{{{20, "20"}, {30, nullptr}, {40, nullptr}, {50, "50"},
	{60, nullptr}, {70, nullptr}, {80, nullptr}, {100, "100"}, {150, nullptr}, {200, "200"},
	{300, nullptr}, {400, nullptr}, {500, "500"}, {700, nullptr}, {1000, "1k"}, {1500, nullptr},
	{2000, "2k"}, {3000, nullptr}, {5000, "5k"}, {7000, nullptr}, {10000, "10k"}, {20000, "20k"}}};

} // namespace




EqDisplay::EqDisplay(EqControls* controls, QWidget* parent) :
	QWidget(parent),
	m_controls(controls)
{
	setMouseTracking(true);
	setFocusPolicy(Qt::StrongFocus);
	setCursor(Qt::CrossCursor);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	attachFocusRing(this);
	setToolTip(tr("Drag a node to set frequency and gain, hold Shift for fine moves and %1 to keep "
		"one of them. Wheel over a node changes its width, wheel on the background zooms the dB scale. "
		"Double-click to switch a band on or off, %2-click to listen to one.").arg(UI_CTRL_KEY).arg(UI_ALT_KEY));

	const auto setBand = [this](int index, EqBandType type, const QString& name, FloatModel* freq,
		FloatModel* gain, FloatModel* q, BoolModel* active, IntModel* slope)
	{
		EqBand& band = m_bands[index];
		band.type = type;
		band.name = name;
		band.color = nodeColor(index);
		band.freq = freq;
		band.gain = gain;
		band.q = q;
		band.active = active;
		band.slope = slope;
	};

	auto* c = m_controls;
	setBand(0, EqBandType::HighPass, tr("High-pass"), &c->m_hpFeqModel, nullptr, &c->m_hpResModel,
		&c->m_hpActiveModel, &c->m_hpTypeModel);
	setBand(1, EqBandType::LowShelf, tr("Low shelf"), &c->m_lowShelfFreqModel, &c->m_lowShelfGainModel,
		&c->m_lowShelfResModel, &c->m_lowShelfActiveModel, nullptr);
	setBand(2, EqBandType::Peak, tr("Peak 1"), &c->m_para1FreqModel, &c->m_para1GainModel,
		&c->m_para1BwModel, &c->m_para1ActiveModel, nullptr);
	setBand(3, EqBandType::Peak, tr("Peak 2"), &c->m_para2FreqModel, &c->m_para2GainModel,
		&c->m_para2BwModel, &c->m_para2ActiveModel, nullptr);
	setBand(4, EqBandType::Peak, tr("Peak 3"), &c->m_para3FreqModel, &c->m_para3GainModel,
		&c->m_para3BwModel, &c->m_para3ActiveModel, nullptr);
	setBand(5, EqBandType::Peak, tr("Peak 4"), &c->m_para4FreqModel, &c->m_para4GainModel,
		&c->m_para4BwModel, &c->m_para4ActiveModel, nullptr);
	setBand(6, EqBandType::HighShelf, tr("High shelf"), &c->m_highShelfFreqModel,
		&c->m_highShelfGainModel, &c->m_highShelfResModel, &c->m_highShelfActiveModel, nullptr);
	setBand(7, EqBandType::LowPass, tr("Low-pass"), &c->m_lpFreqModel, nullptr, &c->m_lpResModel,
		&c->m_lpActiveModel, &c->m_lpTypeModel);

	for (const EqBand& band : m_bands)
	{
		const std::array<AutomatableModel*, 5> models{band.freq, band.gain, band.q, band.active, band.slope};
		for (AutomatableModel* model : models)
		{
			if (model) { connect(model, &Model::dataChanged, this, &EqDisplay::invalidateCurves); }
		}
	}
	connect(&c->m_channelModeModel, &Model::dataChanged, this, [this] { update(); });
	connect(getGUI()->mainWindow(), &MainWindow::periodicUpdate, this, &EqDisplay::refresh);

	m_plot = QRectF(rect()).adjusted(1, 1, -1, -1);
	updateAccessibleText();
}




void EqDisplay::selectBand(int band)
{
	band = std::clamp(band, 0, BandCount - 1);
	if (band == m_selected) { return; }
	m_selected = band;
	updateAccessibleText();
	update();
	emit bandSelected(band);
}




void EqDisplay::refresh()
{
	if (!isVisible()) { return; }

	const bool analyseIn = m_controls->m_analyseInModel.value();
	const bool analyseOut = m_controls->m_analyseOutModel.value();
	// asking for another analysis; the audio thread clears the bands when the toggle is off
	m_controls->m_inFftBands.setActive(analyseIn);
	m_controls->m_outFftBands.setActive(analyseOut);
	updateBars(&m_controls->m_inFftBands, m_inBars, analyseIn);
	updateBars(&m_controls->m_outFftBands, m_outBars, analyseOut);
	update();
}




void EqDisplay::invalidateCurves()
{
	m_curvesDirty = true;
	updateAccessibleText();
	update();
}




// ----------------------------------------------------------------------
// Axes
// ----------------------------------------------------------------------

float EqDisplay::freqToX(float freq) const
{
	static const float range = std::log10(MaxFreq / MinFreq);
	const float position = std::log10(std::max(freq, MinFreq) / MinFreq) / range;
	return m_plot.left() + position * m_plot.width();
}




float EqDisplay::xToFreq(float x) const
{
	static const float range = std::log10(MaxFreq / MinFreq);
	const float position = std::clamp<float>((x - m_plot.left()) / m_plot.width(), 0.f, 1.f);
	return MinFreq * std::pow(10.f, position * range);
}




float EqDisplay::gainToY(float gainDb) const
{
	return m_plot.center().y() - gainDb / m_dbRange * m_plot.height() * 0.5;
}




float EqDisplay::yToGain(float y) const
{
	return (m_plot.center().y() - y) / (m_plot.height() * 0.5) * m_dbRange;
}




QPointF EqDisplay::nodePos(int band) const
{
	const EqBand& b = m_bands[band];
	const float freq = b.freq->value();
	// the pass filters have no gain of their own, so their node rides on their cutoff
	const float gainDb = b.hasGain() ? b.gain->value() : bandResponseDb(band, freq);
	return QPointF(freqToX(freq), std::clamp<float>(gainToY(gainDb), m_plot.top() + 3, m_plot.bottom() - 3));
}




int EqDisplay::bandAt(const QPoint& pos) const
{
	int closest = -1;
	double closestDistance = GrabRadius;
	for (int i = 0; i < BandCount; ++i)
	{
		const double distance = QLineF(nodePos(i), QPointF(pos)).length();
		// ties go to the selected band, so stacked nodes stay grabbable
		if (distance < closestDistance || (i == m_selected && distance <= closestDistance))
		{
			closest = i;
			closestDistance = distance;
		}
	}
	return closest;
}




float EqDisplay::bandResponseDb(int band, float freq) const
{
	const double sampleRate = m_curveSampleRate > 0 ? m_curveSampleRate : 44100.0;
	return m_cascades[band] * static_cast<float>(magnitudeDb(m_coeffs[band], freq, sampleRate));
}




int EqDisplay::freeBandFor(float freq) const
{
	// the peaks can sit anywhere, so they are taken first
	for (const auto& candidates : {std::array<int, 4>{2, 3, 4, 5}, std::array<int, 4>{1, 6, 0, 7}})
	{
		int closest = -1;
		float closestDistance = 0.f;
		for (int band : candidates)
		{
			if (m_bands[band].active->value()) { continue; }
			const float distance = std::abs(std::log2(m_bands[band].freq->value() / freq));
			if (closest < 0 || distance < closestDistance)
			{
				closest = band;
				closestDistance = distance;
			}
		}
		if (closest >= 0) { return closest; }
	}
	return -1;
}




// ----------------------------------------------------------------------
// Response and spectrum
// ----------------------------------------------------------------------

void EqDisplay::ensureCurves()
{
	if (m_curvesDirty || m_curveSampleRate != static_cast<int>(Engine::audioEngine()->outputSampleRate()))
	{
		buildCurves();
	}
}




void EqDisplay::buildCurves()
{
	m_curvesDirty = false;
	m_curveSampleRate = Engine::audioEngine()->outputSampleRate();
	const double sampleRate = m_curveSampleRate > 0 ? m_curveSampleRate : 44100.0;

	for (int i = 0; i < BandCount; ++i)
	{
		m_coeffs[i] = bandCoefficients(m_bands[i], sampleRate);
		// 12, 24 and 48 dB per octave are one, two and four cascaded sections
		m_cascades[i] = m_bands[i].isPass() ? 1 << m_bands[i].slope->value() : 1;
	}

	m_sumPath = QPainterPath();
	m_sumFill = QPainterPath();
	for (QPainterPath& path : m_bandPaths) { path = QPainterPath(); }

	const int columns = std::max(2, static_cast<int>(m_plot.width()));
	const float top = m_plot.top() - 8;
	const float bottom = m_plot.bottom() + 8;
	for (int x = 0; x < columns; ++x)
	{
		const float freq = xToFreq(m_plot.left() + x);
		float sum = 0.f;
		for (int i = 0; i < BandCount; ++i)
		{
			if (!m_bands[i].active->value()) { continue; }
			const float gainDb = bandResponseDb(i, freq);
			sum += gainDb;
			const QPointF point(m_plot.left() + x, std::clamp(gainToY(gainDb), top, bottom));
			if (x == 0) { m_bandPaths[i].moveTo(point); }
			else { m_bandPaths[i].lineTo(point); }
		}
		const QPointF point(m_plot.left() + x, std::clamp(gainToY(sum), top, bottom));
		if (x == 0) { m_sumPath.moveTo(point); }
		else { m_sumPath.lineTo(point); }
	}

	// the filled area between the response and the 0 dB line
	m_sumFill = m_sumPath;
	m_sumFill.lineTo(m_sumFill.currentPosition().x(), gainToY(0));
	m_sumFill.lineTo(m_plot.left(), gainToY(0));
	m_sumFill.closeSubpath();
}




void EqDisplay::updateBars(EqAnalyser* analyser, std::vector<float>& bars, bool enabled)
{
	const int columns = std::max(1, static_cast<int>(m_plot.width()));
	if (static_cast<int>(bars.size()) != columns) { bars.assign(columns, 0.f); }

	const auto fade = [&bars]
	{
		for (float& bar : bars) { bar = bar > 0.002f ? bar * 0.86f : 0.f; }
	};

	const float energy = analyser->getEnergy();
	const int sampleRate = analyser->getSampleRate();
	if (!enabled || energy <= 0.f || sampleRate <= 0)
	{
		fade();
		return;
	}

	// fold the FFT bands into pixel columns, keeping the loudest one per column
	m_barScratch.assign(columns, -1.f);
	for (int i = 1; i < MAX_BANDS; ++i)
	{
		const float freq = i * sampleRate / static_cast<float>(MAX_BANDS * 2);
		if (freq < MinFreq) { continue; }
		if (freq > MaxFreq) { break; }
		const int column = std::clamp(static_cast<int>(freqToX(freq) - m_plot.left()), 0, columns - 1);
		m_barScratch[column] = std::max(m_barScratch[column], analyser->m_bands[i]);
	}

	// below a few hundred Hz the log axis gives a column more room than the FFT has bands
	int previous = -1;
	for (int x = 0; x < columns; ++x)
	{
		if (m_barScratch[x] < 0.f) { continue; }
		if (previous >= 0 && x - previous > 1)
		{
			const float from = m_barScratch[previous];
			const float to = m_barScratch[x];
			for (int gap = previous + 1; gap < x; ++gap)
			{
				m_barScratch[gap] = from + (to - from) * (gap - previous) / static_cast<float>(x - previous);
			}
		}
		previous = x;
	}

	for (int x = 0; x < columns; ++x)
	{
		float height = 0.f;
		if (m_barScratch[x] > 0.f)
		{
			const float gainDb = 20.f * std::log10(m_barScratch[x] / energy);
			height = std::clamp((gainDb - SpectrumFloorDb) / -SpectrumFloorDb, 0.f, 1.f) * 0.78f;
		}
		bars[x] = std::max(height, bars[x] > 0.002f ? bars[x] * 0.88f : 0.f);
	}
}




// ----------------------------------------------------------------------
// Painting
// ----------------------------------------------------------------------

void EqDisplay::paintEvent(QPaintEvent*)
{
	ensureCurves();

	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);

	const QRectF screen = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
	p.setPen(QPen(QColor(0, 0, 0, 200), 1));
	p.setBrush(modern::displayColor());
	p.drawRoundedRect(screen, 2, 2);

	p.save();
	p.setClipRect(m_plot);
	paintGrid(p);
	paintScale(p);
	paintBars(p, m_inBars, modern::textColor(), false);
	paintBars(p, m_outBars, modern::secondaryColor(), true);
	paintCurves(p);
	paintNodes(p);

	const int readout = m_dragging >= 0 ? m_dragging : m_hovered;
	if (readout >= 0) { paintReadout(p, readout); }
	p.restore();
}




void EqDisplay::paintGrid(QPainter& p) const
{
	const QColor grid = modern::gridColor();
	QColor brightGrid = grid;
	brightGrid.setAlpha(std::min(255, grid.alpha() * 2));

	p.setRenderHint(QPainter::Antialiasing, false);
	for (const FreqTick& tick : FreqTicks)
	{
		const float x = std::round(freqToX(tick.freq));
		p.setPen(tick.label ? brightGrid : grid);
		p.drawLine(QPointF(x, m_plot.top()), QPointF(x, m_plot.bottom()));
	}

	const float step = m_dbRange <= 9.f ? 3.f : m_dbRange <= 21.f ? 6.f : 12.f;
	for (float gainDb = -m_dbRange + step; gainDb < m_dbRange; gainDb += step)
	{
		const float y = std::round(gainToY(gainDb));
		p.setPen(std::abs(gainDb) < 0.01f ? brightGrid : grid);
		p.drawLine(QPointF(m_plot.left(), y), QPointF(m_plot.right(), y));
	}
	p.setRenderHint(QPainter::Antialiasing, true);
}




void EqDisplay::paintScale(QPainter& p) const
{
	const QFont font = adjustedToPixelSize(p.font(), SMALL_FONT_SIZE);
	const QFontMetricsF fm(font);
	QColor text = modern::dimTextColor();
	text.setAlpha(150);
	p.setFont(font);
	p.setPen(text);

	for (const FreqTick& tick : FreqTicks)
	{
		if (!tick.label) { continue; }
		const QString label = QString::fromLatin1(tick.label);
		const double width = fm.horizontalAdvance(label);
		double x = freqToX(tick.freq) - width / 2;
		x = std::clamp(x, m_plot.left() + 2, m_plot.right() - width - 2);
		p.drawText(QPointF(x, m_plot.bottom() - 3), label);
	}

	const float step = m_dbRange <= 9.f ? 3.f : m_dbRange <= 21.f ? 6.f : 12.f;
	for (float gainDb = -m_dbRange + step; gainDb < m_dbRange; gainDb += step)
	{
		if (std::abs(gainDb) < 0.01f) { continue; }
		const QString label = (gainDb > 0 ? QString("+") : QString()) + QString::number(gainDb, 'f', 0);
		p.drawText(QPointF(m_plot.right() - fm.horizontalAdvance(label) - 3,
			gainToY(gainDb) - 2), label);
	}

	// the bands do not always work on both channels, and that is easy to forget
	if (m_controls->m_channelModeModel.value() != 0)
	{
		QFont bold = font;
		bold.setBold(true);
		p.setFont(bold);
		p.setPen(modern::warningColor());
		p.drawText(QPointF(m_plot.left() + 5, m_plot.top() + fm.height()),
			m_controls->m_channelModeModel.currentText() + tr(" only"));
	}
}




void EqDisplay::paintBars(QPainter& p, const std::vector<float>& bars, const QColor& color, bool outline) const
{
	if (bars.empty()
		|| std::none_of(bars.begin(), bars.end(), [](float bar) { return bar > 0.002f; }))
	{
		return;
	}

	QPainterPath path;
	path.moveTo(m_plot.left(), m_plot.bottom());
	for (std::size_t i = 0; i < bars.size(); ++i)
	{
		path.lineTo(m_plot.left() + i, m_plot.bottom() - bars[i] * m_plot.height());
	}
	path.lineTo(m_plot.left() + bars.size() - 1, m_plot.bottom());
	path.closeSubpath();

	QColor fill = color;
	fill.setAlpha(outline ? 38 : 26);
	p.fillPath(path, fill);
	if (outline)
	{
		QColor line = color;
		line.setAlpha(150);
		p.strokePath(path, QPen(line, 1));
	}
}




void EqDisplay::paintCurves(QPainter& p) const
{
	for (int i = 0; i < BandCount; ++i)
	{
		if (m_bandPaths[i].elementCount() < 2) { continue; }
		const bool emphasised = i == m_hovered || i == m_dragging;
		QColor color = m_bands[i].color;
		color.setAlpha(emphasised ? 230 : i == m_selected ? 150 : 48);
		p.strokePath(m_bandPaths[i], QPen(color, emphasised ? 1.6 : 1.1));
	}

	QColor fill = modern::accentColor();
	fill.setAlpha(45);
	p.fillPath(m_sumFill, fill);
	p.strokePath(m_sumPath, QPen(modern::accentColor(), 2));
}




void EqDisplay::paintNodes(QPainter& p) const
{
	const QFont font = adjustedToPixelSize(p.font(), SMALL_FONT_SIZE);
	const QFontMetricsF fm(font);
	p.setFont(font);

	for (int i = 0; i < BandCount; ++i)
	{
		const EqBand& band = m_bands[i];
		const bool active = band.active->value();
		const QPointF center = nodePos(i);
		const double radius = i == m_hovered || i == m_dragging ? NodeRadius + 1.5 : NodeRadius;

		if (i == m_dragging)
		{
			// crosshair, so the node can be lined up with the spectrum behind it
			QColor guide = band.color;
			guide.setAlpha(90);
			p.setPen(QPen(guide, 1, Qt::DashLine));
			p.drawLine(QPointF(center.x(), m_plot.top()), QPointF(center.x(), m_plot.bottom()));
			if (band.hasGain())
			{
				p.drawLine(QPointF(m_plot.left(), center.y()), QPointF(m_plot.right(), center.y()));
			}
		}

		if (i == m_selected)
		{
			QColor ring = modern::textColor();
			ring.setAlpha(110);
			p.setPen(QPen(ring, 1));
			p.setBrush(Qt::NoBrush);
			p.drawEllipse(center, radius + 3, radius + 3);
		}
		if (i == m_soloBand)
		{
			p.setPen(QPen(modern::accentColor(), 2));
			p.setBrush(Qt::NoBrush);
			p.drawEllipse(center, radius + 5, radius + 5);
		}

		const QColor color = active ? band.color : modern::dimTextColor();
		p.setPen(QPen(color, active ? 1.5 : 1));
		p.setBrush(active ? QBrush(color) : Qt::NoBrush);
		p.drawEllipse(center, radius, radius);

		const QString number = QString::number(i + 1);
		p.setPen(active ? modern::displayColor() : color);
		p.drawText(QPointF(center.x() - fm.horizontalAdvance(number) / 2, center.y() + fm.capHeight() / 2),
			number);
	}
}




void EqDisplay::paintReadout(QPainter& p, int band) const
{
	const EqBand& b = m_bands[band];
	const QString title = b.active->value() ? b.name : b.name + tr(" (off)");
	QString values = formatFreq(b.freq->value());
	if (b.hasGain()) { values += "   " + formatGain(b.gain->value()); }
	values += b.type == EqBandType::Peak
		? "   " + tr("%1 oct").arg(b.q->value(), 0, 'f', 2)
		: "   " + tr("Q %1").arg(b.q->value(), 0, 'f', 2);
	if (b.isPass()) { values += "   " + tr("%1 dB/oct").arg(12 << b.slope->value()); }

	const QFont font = adjustedToPixelSize(p.font(), SMALL_FONT_SIZE);
	const QFontMetricsF fm(font);
	const double width = std::max(fm.horizontalAdvance(title), fm.horizontalAdvance(values)) + 14;
	const double height = fm.height() * 2 + 8;

	const QPointF node = nodePos(band);
	QRectF box(node.x() + 12, node.y() - height - 10, width, height);
	if (box.right() > m_plot.right() - 3) { box.moveLeft(node.x() - 12 - width); }
	box.moveLeft(std::max(box.left(), m_plot.left() + 3));
	if (box.top() < m_plot.top() + 3) { box.moveTop(node.y() + 14); }
	box.moveTop(std::min(box.top(), m_plot.bottom() - 3 - height));

	QColor background = modern::windowColor();
	background.setAlpha(235);
	p.setPen(QPen(QColor(0, 0, 0, 160), 1));
	p.setBrush(background);
	p.drawRoundedRect(box, 3, 3);

	p.setFont(font);
	p.setPen(b.active->value() ? b.color : modern::dimTextColor());
	p.drawText(box.adjusted(7, 4, -7, -height / 2), Qt::AlignLeft | Qt::AlignVCenter, title);
	p.setPen(modern::textColor());
	p.drawText(box.adjusted(7, height / 2, -7, -4), Qt::AlignLeft | Qt::AlignVCenter, values);
}




void EqDisplay::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	m_plot = QRectF(rect()).adjusted(1, 1, -1, -1);
	m_curvesDirty = true;
}




// ----------------------------------------------------------------------
// Editing
// ----------------------------------------------------------------------

void EqDisplay::mousePressEvent(QMouseEvent* event)
{
	ensureCurves();
	setFocus(Qt::MouseFocusReason);
	const int band = bandAt(event->pos());

	if (event->button() == Qt::LeftButton && event->modifiers() & Qt::AltModifier && band >= 0)
	{
		// audition the range the band works on while the button is held
		selectBand(band);
		m_soloBand = band;
		m_controls->soloBandModel().setValue(band);
		update();
		return;
	}
	if (event->button() != Qt::LeftButton || band < 0)
	{
		QWidget::mousePressEvent(event);
		return;
	}

	selectBand(band);
	m_dragging = band;
	m_dragStart = event->pos();
	m_dragFreq = m_bands[band].freq->value();
	m_dragGain = m_bands[band].hasGain() ? m_bands[band].gain->value() : 0.f;
	m_dragMoved = false;
	m_dragAxis = DragAxis::Both;
	update();
}




void EqDisplay::mouseMoveEvent(QMouseEvent* event)
{
	ensureCurves();
	if (m_dragging < 0)
	{
		const int band = bandAt(event->pos());
		setCursor(band >= 0 ? Qt::SizeAllCursor : Qt::CrossCursor);
		if (band != m_hovered)
		{
			m_hovered = band;
			update();
		}
		return;
	}

	const EqBand& band = m_bands[m_dragging];
	const QPoint delta = event->pos() - m_dragStart;
	if (!m_dragMoved && delta.manhattanLength() > 2)
	{
		m_dragMoved = true;
		// grabbing a switched off band and moving it is how it gets switched on
		if (!band.active->value()) { band.active->setValue(true); }
	}

	if (event->modifiers() & Qt::ControlModifier)
	{
		if (m_dragAxis == DragAxis::Both && delta.manhattanLength() > 3)
		{
			m_dragAxis = std::abs(delta.x()) >= std::abs(delta.y()) ? DragAxis::Frequency : DragAxis::Gain;
		}
	}
	else { m_dragAxis = DragAxis::Both; }

	const float scale = event->modifiers() & Qt::ShiftModifier ? 0.25f : 1.f;
	if (m_dragAxis != DragAxis::Gain)
	{
		band.freq->setValue(xToFreq(freqToX(m_dragFreq) + delta.x() * scale));
	}
	if (band.hasGain() && m_dragAxis != DragAxis::Frequency)
	{
		const float dbPerPixel = 2.f * m_dbRange / m_plot.height();
		band.gain->setValue(m_dragGain - delta.y() * scale * dbPerPixel);
	}
	update();
}




void EqDisplay::mouseReleaseEvent(QMouseEvent* event)
{
	if (m_soloBand >= 0)
	{
		m_controls->soloBandModel().setValue(-1);
		m_soloBand = -1;
	}
	if (m_dragging >= 0)
	{
		m_dragging = -1;
		m_hovered = bandAt(event->pos());
	}
	update();
}




void EqDisplay::mouseDoubleClickEvent(QMouseEvent* event)
{
	ensureCurves();
	if (event->button() != Qt::LeftButton)
	{
		QWidget::mouseDoubleClickEvent(event);
		return;
	}

	m_dragging = -1;
	const int band = bandAt(event->pos());
	if (band >= 0)
	{
		setBandActive(band, !m_bands[band].active->value());
		return;
	}

	// empty space: switch on the band that fits the spot, like adding one
	const float freq = xToFreq(event->pos().x());
	const int free = freeBandFor(freq);
	if (free < 0) { return; }
	m_bands[free].freq->setValue(freq);
	if (m_bands[free].hasGain()) { m_bands[free].gain->setValue(yToGain(event->pos().y())); }
	setBandActive(free, true);
	selectBand(free);
}




void EqDisplay::wheelEvent(QWheelEvent* event)
{
	ensureCurves();
	event->accept();
	const int steps = m_wheelSteps.steps(event);
	if (steps == 0) { return; }

	const int band = bandAt(event->position().toPoint());
	if (band >= 0)
	{
		selectBand(band);
		adjustQ(band, steps, event->modifiers() & Qt::ShiftModifier);
	}
	else
	{
		m_dbRange = std::clamp<float>(m_dbRange * std::pow(1.2f, -steps), 6.f, 36.f);
		m_curvesDirty = true;
		update();
	}
}




void EqDisplay::leaveEvent(QEvent* event)
{
	QWidget::leaveEvent(event);
	if (m_hovered >= 0)
	{
		m_hovered = -1;
		update();
	}
}




void EqDisplay::keyPressEvent(QKeyEvent* event)
{
	const bool fine = event->modifiers() & Qt::ShiftModifier;
	const int band = m_selected;

	if (event->key() >= Qt::Key_1 && event->key() <= Qt::Key_8)
	{
		selectBand(event->key() - Qt::Key_1);
		announce(m_selected);
		return;
	}

	switch (event->key())
	{
	case Qt::Key_Left: adjustFreq(band, -1, fine); break;
	case Qt::Key_Right: adjustFreq(band, 1, fine); break;
	case Qt::Key_Down: adjustGain(band, -1, fine); break;
	case Qt::Key_Up: adjustGain(band, 1, fine); break;
	case Qt::Key_BracketLeft:
	case Qt::Key_Minus: adjustQ(band, -1, fine); break;
	case Qt::Key_BracketRight:
	case Qt::Key_Plus:
	case Qt::Key_Equal: adjustQ(band, 1, fine); break;
	case Qt::Key_Space:
	case Qt::Key_Return:
	case Qt::Key_Enter: setBandActive(band, !m_bands[band].active->value()); break;
	case Qt::Key_Delete:
	case Qt::Key_Backspace: setBandActive(band, false); break;
	case Qt::Key_Home: resetBand(band); break;
	default:
		QWidget::keyPressEvent(event);
		return;
	}
	announce(band);
}




void EqDisplay::contextMenuEvent(QContextMenuEvent* event)
{
	ensureCurves();
	const int band = bandAt(event->pos());
	if (band < 0)
	{
		QWidget::contextMenuEvent(event);
		return;
	}

	selectBand(band);
	const EqBand& b = m_bands[band];
	QMenu menu(this);
	menu.addSection(b.name);

	QAction* active = menu.addAction(tr("Active"));
	active->setCheckable(true);
	active->setChecked(b.active->value());
	connect(active, &QAction::triggered, this, [this, band](bool on) { setBandActive(band, on); });

	QAction* reset = menu.addAction(tr("Reset band"));
	connect(reset, &QAction::triggered, this, [this, band] { resetBand(band); });

	if (b.isPass())
	{
		menu.addSection(tr("Slope"));
		for (int slope = 0; slope < 3; ++slope)
		{
			QAction* action = menu.addAction(tr("%1 dB per octave").arg(12 << slope));
			action->setCheckable(true);
			action->setChecked(b.slope->value() == slope);
			connect(action, &QAction::triggered, this, [this, band, slope]
				{ m_bands[band].slope->setValue(slope); });
		}
	}

	menu.exec(event->globalPos());
}




void EqDisplay::setBandActive(int band, bool active)
{
	m_bands[band].active->setValue(active);
	update();
}




void EqDisplay::adjustFreq(int band, int steps, bool fine)
{
	FloatModel* model = m_bands[band].freq;
	// a step is a semitone, so the whole range stays reachable
	model->setValue(model->value() * std::pow(2.f, steps * (fine ? 1.f / 120.f : 1.f / 12.f)));
}




void EqDisplay::adjustGain(int band, int steps, bool fine)
{
	FloatModel* model = m_bands[band].gain;
	if (!model) { return; }
	model->setValue(model->value() + steps * (fine ? 0.1f : 0.5f));
}




void EqDisplay::adjustQ(int band, int steps, bool fine)
{
	FloatModel* model = m_bands[band].q;
	const float span = model->maxValue() - model->minValue();
	float step = span * (fine ? 0.01f : 0.04f) * steps;
	// the peaks are set by bandwidth, so turning the wheel up has to narrow them
	if (m_bands[band].type == EqBandType::Peak) { step = -step; }
	model->setValue(model->value() + step);
}




void EqDisplay::resetBand(int band)
{
	const EqBand& b = m_bands[band];
	if (b.gain) { b.gain->reset(); }
	b.q->reset();
	b.freq->reset();
}




QString EqDisplay::bandSummary(int band) const
{
	const EqBand& b = m_bands[band];
	QString summary = tr("Band %1, %2, %3").arg(band + 1).arg(b.name)
		.arg(b.active->value() ? tr("on") : tr("off"));
	summary += ", " + formatFreq(b.freq->value());
	if (b.hasGain()) { summary += ", " + formatGain(b.gain->value()); }
	summary += b.type == EqBandType::Peak
		? ", " + tr("bandwidth %1 octaves").arg(b.q->value(), 0, 'f', 2)
		: ", " + tr("resonance %1").arg(b.q->value(), 0, 'f', 2);
	if (b.isPass()) { summary += ", " + tr("%1 dB per octave").arg(12 << b.slope->value()); }
	return summary;
}




void EqDisplay::updateAccessibleText()
{
	setAccessibleName(tr("Equalizer bands"));
	setAccessibleDescription(bandSummary(m_selected) + ". "
		+ tr("Keys 1 to 8 select a band, arrow keys change frequency and gain, brackets change the "
			"width, space switches the band on and off."));
}




void EqDisplay::announce(int band)
{
	updateAccessibleText();
	announceValueChange(this, m_bands[band].freq->value());
}


} // namespace lmms::gui
