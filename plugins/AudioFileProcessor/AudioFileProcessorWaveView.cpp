/*
 * AudioFileProcessorWaveView.cpp - Wave renderer of the AFP
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


#include "AudioFileProcessorWaveView.h"

#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <cmath>
#include <numbers>

#include "AudioFileProcessor.h"
#include "ConfigManager.h"
#include "DeprecationHelper.h"
#include "FontHelper.h"
#include "InstrumentTrack.h"
#include "MidiEvent.h"
#include "ModernWidgets.h"
#include "Sample.h"
#include "Song.h"


namespace lmms
{

namespace gui
{

namespace
{

constexpr int PadLaneHeight = 16;
constexpr int RulerHeight = 14;
constexpr int OverviewHeight = 12;
constexpr int GrabDistance = 4;
constexpr int FadeHandleSize = 7;

QColor clipColor() { return QColor(255, 92, 92); }
QColor loopColor() { return modern::secondaryColor(); }

QString keyName(int key)
{
	static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
	if (key < 0) { return {}; }
	return QString("%1%2").arg(names[key % 12]).arg(key / 12 - 1);
}

} // namespace


AudioFileProcessorWaveView::AudioFileProcessorWaveView(QWidget* parent, AudioFileProcessor* afp) :
	QWidget(parent),
	m_afp(afp),
	m_timer(new QTimer(this))
{
	setMouseTracking(true);
	setFont(adjustedToPixelSize(font(), SMALL_FONT_SIZE));
	setAttribute(Qt::WA_OpaquePaintEvent);

	const auto redraw = [this] { m_waveDirty = true; update(); };
	connect(m_afp, &AudioFileProcessor::dataChanged, this, redraw);
	connect(m_afp, &AudioFileProcessor::slicesChanged, this, qOverload<>(&QWidget::update));
	connect(&m_afp->modeModel(), &Model::dataChanged, this, [this] { m_hover = {}; update(); });
	for (Model* model : std::initializer_list<Model*>{&m_afp->loopModel(),
		&m_afp->fadeInModel(), &m_afp->fadeOutModel(), m_afp->instrumentTrack()->baseNoteModel()})
	{
		connect(model, &Model::dataChanged, this, qOverload<>(&QWidget::update));
	}

	// The playback cursor is polled instead of signalled from the audio thread
	connect(m_timer, &QTimer::timeout, this, [this]
	{
		const auto tick = m_afp->playTick();
		if (tick != m_lastPlayTick)
		{
			m_lastPlayTick = tick;
			m_playFrame = m_afp->playFrame();
			m_idleTicks = 0;
			update();
		}
		else if (m_playFrame >= 0 && ++m_idleTicks > 3)
		{
			m_playFrame = -1;
			update();
		}
	});
	// The cursor can be switched off in the settings ("waveform display animation")
	if (ConfigManager::inst()->value("ui", "animateafp", "1").toInt()) { m_timer->start(33); }

	sampleUpdated();
}




void AudioFileProcessorWaveView::sampleUpdated()
{
	const auto& sample = m_afp->sample();
	if (sample.buffer() != m_peaksBuffer)
	{
		const auto old = m_peaksBuffer;
		rebuildPeaks();
		// A new file shows in full; warping the same file keeps the zoom on the same audio
		if (old && old->size() > 1 && old->audioFile() == sample.sampleFile() && sample.sampleSize() > 1)
		{
			const double scale = static_cast<double>(sample.sampleSize()) / old->size();
			setView(m_viewFrom * scale, m_viewTo * scale);
		}
		else { zoomToFit(); }
	}
	if (sample.reversed() != m_reversed)
	{
		m_reversed = sample.reversed();
		setView(sampleSize() - m_viewTo, sampleSize() - m_viewFrom);
	}
	m_waveDirty = true;
	update();
}




void AudioFileProcessorWaveView::rebuildPeaks()
{
	m_peaksBuffer = m_afp->sample().buffer();
	m_peaks.clear();
	if (!m_peaksBuffer) { return; }

	const auto size = m_peaksBuffer->size();
	const auto data = m_peaksBuffer->data();
	m_peaks.resize((size + BlockSize - 1) / BlockSize);
	for (std::size_t b = 0; b < m_peaks.size(); ++b)
	{
		Peak peak{1.f, -1.f};
		for (std::size_t i = b * BlockSize; i < std::min(size, (b + 1) * BlockSize); ++i)
		{
			peak.min = std::min({peak.min, data[i].left(), data[i].right()});
			peak.max = std::max({peak.max, data[i].left(), data[i].right()});
		}
		m_peaks[b] = peak;
	}
}




int AudioFileProcessorWaveView::sampleSize() const
{
	return static_cast<int>(m_afp->sample().sampleSize());
}




void AudioFileProcessorWaveView::zoomToFit()
{
	m_viewFrom = 0.0;
	m_viewTo = std::max(1, sampleSize());
	m_waveDirty = true;
	update();
}




void AudioFileProcessorWaveView::setView(double from, double to)
{
	const double size = std::max(1, sampleSize());
	// Don't zoom in further than about 4 pixels per frame
	const double minSpan = std::min(size, std::max(16.0, waveRect().width() / 4.0));
	double span = std::clamp(to - from, minSpan, size);
	from = std::clamp(from, 0.0, size - span);
	m_viewFrom = from;
	m_viewTo = from + span;
	m_waveDirty = true;
	update();
}




void AudioFileProcessorWaveView::zoomAt(double x, double factor)
{
	const double anchor = xToFrame(x);
	setView(anchor - (anchor - m_viewFrom) * factor, anchor + (m_viewTo - anchor) * factor);
}




QRect AudioFileProcessorWaveView::padLane() const
{
	return QRect(0, 0, width(), PadLaneHeight);
}

QRect AudioFileProcessorWaveView::waveRect() const
{
	return QRect(0, PadLaneHeight, width(), height() - PadLaneHeight - RulerHeight - OverviewHeight);
}

QRect AudioFileProcessorWaveView::rulerRect() const
{
	return QRect(0, waveRect().bottom() + 1, width(), RulerHeight);
}

QRect AudioFileProcessorWaveView::overviewRect() const
{
	return QRect(0, height() - OverviewHeight, width(), OverviewHeight);
}




double AudioFileProcessorWaveView::frameToX(double frame) const
{
	return (frame - m_viewFrom) / std::max(1e-9, m_viewTo - m_viewFrom) * width();
}

double AudioFileProcessorWaveView::xToFrame(double x) const
{
	return m_viewFrom + x / std::max(1, width()) * (m_viewTo - m_viewFrom);
}

double AudioFileProcessorWaveView::fadeFrames(float ms) const
{
	return ms * m_afp->sample().sampleRate() / 1000.0;
}




AudioFileProcessorWaveView::Peak AudioFileProcessorWaveView::peakOf(double from, double to) const
{
	const auto size = sampleSize();
	if (size == 0 || !m_peaksBuffer) { return {}; }

	// Logical (playing order) to buffer order
	auto a = static_cast<int>(std::floor(from));
	auto b = static_cast<int>(std::ceil(to));
	if (m_reversed) { std::tie(a, b) = std::pair{size - b, size - a}; }
	a = std::clamp(a, 0, size - 1);
	b = std::clamp(b, a + 1, size);

	Peak peak{1.f, -1.f};
	if (b - a >= 2 * BlockSize)
	{
		for (int block = a / BlockSize; block <= (b - 1) / BlockSize; ++block)
		{
			peak.min = std::min(peak.min, m_peaks[block].min);
			peak.max = std::max(peak.max, m_peaks[block].max);
		}
	}
	else
	{
		const auto data = m_peaksBuffer->data();
		for (int i = a; i < b; ++i)
		{
			peak.min = std::min({peak.min, data[i].left(), data[i].right()});
			peak.max = std::max({peak.max, data[i].left(), data[i].right()});
		}
	}
	return peak;
}




void AudioFileProcessorWaveView::renderWave()
{
	const QRect rect = waveRect();
	if (m_wave.size() != rect.size()) { m_wave = QPixmap(rect.size()); }
	m_wave.fill(Qt::transparent);
	m_waveDirty = false;

	const auto& sample = m_afp->sample();
	const int size = sampleSize();
	if (size <= 1) { return; }

	QPainter p(&m_wave);
	const float amp = sample.amplification();
	const double mid = rect.height() / 2.0;
	const double half = rect.height() / 2.0 - 2.0;
	const int start = sample.startFrame();
	const int end = sample.endFrame();

	p.setPen(QPen(modern::gridColor(), 1));
	p.drawLine(QPointF(0, mid), QPointF(rect.width(), mid));

	const double framesPerPixel = (m_viewTo - m_viewFrom) / rect.width();
	const auto colorAt = [&](double frame)
	{
		return frame >= start && frame < end ? modern::accentColor() : modern::dimTextColor();
	};

	if (framesPerPixel < 1.5)
	{
		// Zoomed in far enough to see single samples: draw the waveform as a line
		p.setRenderHint(QPainter::Antialiasing);
		const auto data = sample.data();
		const int first = std::max(0, static_cast<int>(std::floor(m_viewFrom)) - 1);
		const int last = std::min(size - 1, static_cast<int>(std::ceil(m_viewTo)) + 1);
		QPointF previous;
		for (int i = first; i <= last; ++i)
		{
			const auto& frame = data[m_reversed ? size - i - 1 : i];
			const float value = std::clamp((frame.left() + frame.right()) * 0.5f * amp, -1.f, 1.f);
			const QPointF point(frameToX(i + 0.5), mid - value * half);
			if (i > first)
			{
				p.setPen(QPen(colorAt(i), 1.3));
				p.drawLine(previous, point);
			}
			if (framesPerPixel < 0.2)
			{
				p.setPen(Qt::NoPen);
				p.setBrush(colorAt(i));
				p.drawEllipse(point, 1.8, 1.8);
			}
			previous = point;
		}
		return;
	}

	for (int x = 0; x < rect.width(); ++x)
	{
		const double from = xToFrame(x);
		const double to = xToFrame(x + 1);
		if (to <= 0 || from >= size) { continue; }
		const auto peak = peakOf(std::max(0.0, from), std::min<double>(size, to));
		const float lo = peak.min * amp;
		const float hi = peak.max * amp;

		p.setPen(colorAt((from + to) / 2));
		const double yTop = mid - std::min(hi, 1.f) * half;
		const double yBottom = mid - std::max(lo, -1.f) * half;
		p.drawLine(QPointF(x + 0.5, yTop), QPointF(x + 0.5, std::max(yBottom, yTop + 1)));

		// Mark where the amplified signal would clip
		if (hi > 1.f || lo < -1.f)
		{
			p.setPen(clipColor());
			if (hi > 1.f) { p.drawLine(QPointF(x + 0.5, mid - half), QPointF(x + 0.5, mid - half + 3)); }
			if (lo < -1.f) { p.drawLine(QPointF(x + 0.5, mid + half - 3), QPointF(x + 0.5, mid + half)); }
		}
	}
}




void AudioFileProcessorWaveView::resizeEvent(QResizeEvent*)
{
	m_waveDirty = true;
}




QString AudioFileProcessorWaveView::formatTime(double seconds, double step) const
{
	if (step < 0.01) { return QString("%1 ms").arg(seconds * 1000.0, 0, 'f', step < 0.001 ? 1 : 0); }
	if (seconds >= 60.0)
	{
		const int minutes = static_cast<int>(seconds / 60.0);
		return QString("%1:%2").arg(minutes).arg(seconds - minutes * 60.0, step < 1.0 ? 4 : 2, 'f', step < 1.0 ? 1 : 0, '0');
	}
	return QString("%1 s").arg(seconds, 0, 'f', step < 0.1 ? 2 : (step < 1.0 ? 1 : 0));
}




void AudioFileProcessorWaveView::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	const auto& sample = m_afp->sample();
	const int size = sampleSize();
	const QRect wave = waveRect();
	const QRect lane = padLane();
	const QRect ruler = rulerRect();
	const QRect overview = overviewRect();
	const bool slicing = m_afp->sliceMode();
	const auto loopMode = static_cast<Sample::Loop>(m_afp->loopModel().value());

	p.fillRect(rect(), modern::displayColor());
	p.fillRect(lane, modern::sectionColor());
	p.fillRect(overview, modern::sectionColor());

	if (size <= 1)
	{
		p.setPen(modern::dimTextColor());
		p.drawText(wave, Qt::AlignCenter, tr("Drop a sample here, or click to open one"));
		return;
	}

	const double startX = frameToX(sample.startFrame());
	const double endX = frameToX(sample.endFrame());
	const double loopX = frameToX(sample.loopStartFrame());
	const double fadeInX = frameToX(sample.startFrame() + fadeFrames(m_afp->fadeInModel().value()));
	const double fadeOutX = frameToX(sample.endFrame() - fadeFrames(m_afp->fadeOutModel().value()));
	const auto lineX = [](double x) { return std::floor(x) + 0.5; };

	// Loop range tint
	if (!slicing && loopMode != Sample::Loop::Off)
	{
		auto tint = loopColor();
		tint.setAlpha(26);
		p.fillRect(QRectF(loopX, wave.top(), endX - loopX, wave.height()), tint);
	}

	if (m_waveDirty || m_wave.size() != wave.size()) { renderWave(); }
	p.drawPixmap(wave.topLeft(), m_wave);

	// Everything outside the start..end region is dimmed
	const QColor shade(0, 0, 0, 110);
	p.fillRect(QRectF(0, wave.top(), std::max(0.0, startX), wave.height()), shade);
	p.fillRect(QRectF(endX, wave.top(), std::max(0.0, width() - endX), wave.height()), shade);

	p.setRenderHint(QPainter::Antialiasing);

	// Fades: the part of the waveform they take away is shaded, with the curve on top
	if (!slicing)
	{
		const auto drawFade = [&](double fromX, double toX, bool fadeIn, bool active)
		{
			if (std::abs(toX - fromX) < 1.0) { return; }
			QPainterPath curve;
			QPainterPath cut;
			const int steps = std::clamp(static_cast<int>(std::abs(toX - fromX) / 2), 4, 200);
			cut.moveTo(fromX, wave.top());
			for (int i = 0; i <= steps; ++i)
			{
				const double t = static_cast<double>(i) / steps;
				const double gain = fadeIn ? std::sin(t * std::numbers::pi / 2) : std::cos(t * std::numbers::pi / 2);
				const QPointF point(fromX + (toX - fromX) * t, wave.bottom() - gain * (wave.height() - 4));
				if (i == 0) { curve.moveTo(point); } else { curve.lineTo(point); }
				cut.lineTo(point);
			}
			cut.lineTo(toX, wave.top());
			cut.closeSubpath();
			p.fillPath(cut, QColor(0, 0, 0, active ? 120 : 50));
			auto color = modern::textColor();
			color.setAlpha(active ? 200 : 70);
			p.strokePath(curve, QPen(color, 1.2));
		};
		drawFade(startX, fadeInX, true, true);
		drawFade(fadeOutX, endX, false, loopMode == Sample::Loop::Off);
	}

	p.setRenderHint(QPainter::Antialiasing, false);

	// Slices: markers across the waveform and a pad per slice in the top lane
	if (slicing)
	{
		const auto bounds = m_afp->sliceBoundaries();
		const int baseNote = m_afp->instrumentTrack()->baseNote();
		const int playing = m_playFrame >= 0 ? m_afp->playKey() - baseNote : -1;
		const auto fm = QFontMetrics(font());
		for (std::size_t i = 0; i + 1 < bounds.size(); ++i)
		{
			const double a = frameToX(bounds[i]);
			const double b = frameToX(bounds[i + 1]);
			if (b < 0 || a > width()) { continue; }
			const QRectF pad(std::floor(a) + 1, lane.top() + 1, std::floor(b) - std::floor(a) - 1, lane.height() - 2);
			const bool active = static_cast<int>(i) == playing
				|| (m_drag.target == Target::Pad && m_drag.index == static_cast<int>(i));
			const bool hovered = m_hover.target == Target::Pad && m_hover.index == static_cast<int>(i);
			p.fillRect(pad, active ? modern::activeButtonColor() : (hovered ? modern::buttonColor().lighter(130) : modern::buttonColor()));
			const auto label = QString::number(i + 1);
			if (pad.width() > fm.horizontalAdvance(label) + 4)
			{
				p.setPen(active ? modern::accentColor() : modern::textColor());
				p.drawText(pad, Qt::AlignCenter, label);
			}
			if (active)
			{
				// Light up the slice that is playing
				auto glow = modern::accentColor();
				glow.setAlpha(28);
				p.fillRect(QRectF(a, wave.top(), b - a, wave.height()), glow);
			}
		}
		for (std::size_t i = 0; i < m_afp->slices().size(); ++i)
		{
			const double frame = m_afp->slices()[i] * size;
			const double x = lineX(frameToX(frame));
			if (x < 0 || x > width()) { continue; }
			const bool inside = frame > sample.startFrame() && frame < sample.endFrame();
			const bool highlighted = (m_hover.target == Target::Marker || m_drag.target == Target::Marker)
				&& (m_drag.target == Target::Marker ? m_drag.index : m_hover.index) == static_cast<int>(i);
			auto color = highlighted ? modern::accentColor() : modern::textColor();
			color.setAlpha(highlighted ? 255 : (inside ? 150 : 50));
			p.setPen(QPen(color, 1));
			p.drawLine(QPointF(x, wave.top()), QPointF(x, wave.bottom()));
		}
	}

	// Start, loop and end points
	const auto drawPoint = [&](double x, const QColor& color, const QString& flag, bool flagRight, Target target)
	{
		if (x < -10 || x > width() + 10) { return; }
		const bool highlighted = m_hover.target == target || m_drag.target == target;
		const QColor c = highlighted ? modern::accentColor() : color;
		p.setPen(QPen(c, 1));
		p.drawLine(QPointF(lineX(x), wave.top()), QPointF(lineX(x), wave.bottom()));
		// A grip at the bottom of the line
		QPolygonF grip;
		const double bottom = wave.bottom() + 1;
		grip << QPointF(lineX(x), bottom - 7) << QPointF(lineX(x) + (flagRight ? 6 : -6), bottom) << QPointF(lineX(x), bottom);
		p.setPen(Qt::NoPen);
		p.setBrush(c);
		p.drawPolygon(grip);
		if (!flag.isEmpty())
		{
			const QRectF tab(flagRight ? std::floor(x) : std::floor(x) - 13 + 1, lane.top(), 13, lane.height());
			p.fillRect(tab, c);
			p.setPen(modern::displayColor());
			p.drawText(tab, Qt::AlignCenter, flag);
		}
	};
	const auto pointColor = modern::textColor();
	if (!slicing && loopMode != Sample::Loop::Off) { drawPoint(loopX, loopColor(), "L", true, Target::Loop); }
	drawPoint(startX, pointColor, slicing ? QString() : "S", true, Target::Start);
	drawPoint(endX, pointColor, slicing ? QString() : "E", false, Target::End);

	// Fade handles
	if (!slicing)
	{
		p.setRenderHint(QPainter::Antialiasing);
		const auto drawHandle = [&](double x, Target target, bool enabled)
		{
			const bool highlighted = m_hover.target == target || m_drag.target == target;
			const QRectF handle(lineX(x) - FadeHandleSize / 2.0, wave.top() + 3, FadeHandleSize, FadeHandleSize);
			p.setPen(QPen(highlighted ? modern::accentColor() : modern::textColor(), 1));
			p.setBrush(highlighted ? modern::accentColor() : (enabled ? modern::displayColor() : modern::sectionColor()));
			p.drawRect(handle);
		};
		drawHandle(fadeInX, Target::FadeIn, true);
		drawHandle(fadeOutX, Target::FadeOut, loopMode == Sample::Loop::Off);
		p.setRenderHint(QPainter::Antialiasing, false);
	}

	// Playback cursor
	if (m_playFrame >= 0)
	{
		const double x = lineX(frameToX(m_playFrame));
		p.setPen(QPen(QColor(255, 255, 255, 200), 1));
		p.drawLine(QPointF(x, wave.top()), QPointF(x, wave.bottom()));
	}

	// Time ruler
	{
		const double seconds = (m_viewTo - m_viewFrom) / std::max(1, sample.sampleRate());
		const double target = seconds * 80.0 / std::max(1, width());
		static constexpr double steps[] = {0.0005, 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.25, 0.5,
			1, 2, 5, 10, 15, 30, 60, 120, 300};
		double step = steps[std::size(steps) - 1];
		for (double s : steps) { if (s >= target) { step = s; break; } }
		const double sr = std::max(1, sample.sampleRate());
		p.setPen(modern::gridColor());
		p.drawLine(ruler.topLeft(), ruler.topRight());
		const auto fm = QFontMetrics(font());
		for (double t = std::ceil(m_viewFrom / sr / step) * step; t * sr <= m_viewTo; t += step)
		{
			const double x = lineX(frameToX(t * sr));
			p.setPen(modern::dimTextColor());
			p.drawLine(QPointF(x, ruler.top()), QPointF(x, ruler.top() + 3));
			const auto label = formatTime(t, step);
			if (x + 3 + fm.horizontalAdvance(label) < width())
			{
				p.drawText(QPointF(x + 3, ruler.top() + 3 + fm.capHeight() + 1), label);
			}
		}
	}

	// Overview of the whole sample with the visible part outlined
	{
		const double framesPerPixel = static_cast<double>(size) / overview.width();
		const double mid = overview.center().y() + 0.5;
		const double half = overview.height() / 2.0 - 1.0;
		const float amp = sample.amplification();
		auto color = modern::dimTextColor();
		for (int x = 0; x < overview.width(); ++x)
		{
			const auto peak = peakOf(x * framesPerPixel, (x + 1) * framesPerPixel);
			const double frame = (x + 0.5) * framesPerPixel;
			p.setPen(frame >= sample.startFrame() && frame < sample.endFrame() ? modern::accentColor().darker(150) : color);
			p.drawLine(QPointF(x + 0.5, mid - std::min(peak.max * amp, 1.f) * half),
				QPointF(x + 0.5, mid - std::max(peak.min * amp, -1.f) * half));
		}
		if (m_viewTo - m_viewFrom < size - 1)
		{
			const double a = m_viewFrom / framesPerPixel;
			const double b = m_viewTo / framesPerPixel;
			auto window = modern::textColor();
			window.setAlpha(40);
			p.fillRect(QRectF(a, overview.top(), std::max(2.0, b - a), overview.height()), window);
			window.setAlpha(160);
			p.setPen(window);
			p.setBrush(Qt::NoBrush);
			p.drawRect(QRectF(std::floor(a) + 0.5, overview.top() + 0.5, std::max(2.0, std::floor(b - a) - 1.0), overview.height() - 1.0));
		}
		if (m_playFrame >= 0)
		{
			p.setPen(QColor(255, 255, 255, 200));
			const double x = lineX(m_playFrame / framesPerPixel);
			p.drawLine(QPointF(x, overview.top()), QPointF(x, overview.bottom()));
		}
	}

	// What the hovered or dragged control is set to
	if (const auto text = hoverText(); !text.isEmpty())
	{
		const auto fm = QFontMetrics(font());
		const QRect box(wave.right() - fm.horizontalAdvance(text) - 12, wave.top() + 3, fm.horizontalAdvance(text) + 8, fm.height() + 2);
		p.fillRect(box, QColor(0, 0, 0, 170));
		p.setPen(modern::textColor());
		p.drawText(box, Qt::AlignCenter, text);
	}
}




QString AudioFileProcessorWaveView::hoverText() const
{
	const auto& hit = m_drag.target != Target::None ? m_drag : m_hover;
	const auto& sample = m_afp->sample();
	// The hover may be left over from the other mode
	const bool slicing = m_afp->sliceMode();
	const bool sliceTarget = hit.target == Target::Pad || hit.target == Target::Marker;
	const bool classicTarget = hit.target == Target::FadeIn || hit.target == Target::FadeOut || hit.target == Target::Loop;
	if ((sliceTarget && !slicing) || (classicTarget && slicing)) { return {}; }
	const double sr = std::max(1, sample.sampleRate());
	const auto seconds = [&](double frames) { return formatTime(frames / sr, frames / sr < 1.0 ? 0.001 : 0.01); };
	switch (hit.target)
	{
		case Target::Start: return tr("Start %1").arg(seconds(sample.startFrame()));
		case Target::End: return tr("End %1").arg(seconds(sample.endFrame()));
		case Target::Loop: return tr("Loop from %1").arg(seconds(sample.loopStartFrame()));
		case Target::FadeIn: return tr("Fade in %1 ms").arg(m_afp->fadeInModel().value(), 0, 'f', 0);
		case Target::FadeOut:
			return m_afp->loopModel().value() == static_cast<int>(Sample::Loop::Off)
				? tr("Fade out %1 ms").arg(m_afp->fadeOutModel().value(), 0, 'f', 0)
				: tr("Fade out (off while looping)");
		case Target::Marker:
			if (hit.index >= 0 && hit.index < static_cast<int>(m_afp->slices().size()))
			{
				return tr("Slice at %1").arg(seconds(m_afp->slices()[hit.index] * sampleSize()));
			}
			break;
		case Target::Pad:
		{
			const auto bounds = m_afp->sliceBoundaries();
			if (hit.index >= 0 && hit.index + 1 < static_cast<int>(bounds.size()))
			{
				return tr("Slice %1 · %2 · %3").arg(hit.index + 1)
					.arg(keyName(m_afp->instrumentTrack()->baseNote() + hit.index))
					.arg(seconds(bounds[hit.index + 1] - bounds[hit.index]));
			}
			break;
		}
		default: break;
	}
	return {};
}




AudioFileProcessorWaveView::Hit AudioFileProcessorWaveView::hitTest(const QPoint& pos) const
{
	if (sampleSize() <= 1) { return {}; }
	if (overviewRect().contains(pos)) { return {Target::Overview}; }

	const auto& sample = m_afp->sample();
	const bool slicing = m_afp->sliceMode();
	const auto near = [&](double frame) { return std::abs(frameToX(frame) - pos.x()) <= GrabDistance; };

	if (!slicing)
	{
		// Fade handles sit at the top of the waveform
		if (pos.y() >= waveRect().top() && pos.y() <= waveRect().top() + FadeHandleSize + 6)
		{
			if (near(sample.startFrame() + fadeFrames(m_afp->fadeInModel().value()))) { return {Target::FadeIn}; }
			if (near(sample.endFrame() - fadeFrames(m_afp->fadeOutModel().value()))) { return {Target::FadeOut}; }
		}

		// The closest of the points within reach
		Hit best;
		double bestDistance = GrabDistance + 1;
		const auto consider = [&](double frame, Target target)
		{
			const double distance = std::abs(frameToX(frame) - pos.x());
			if (distance < bestDistance) { bestDistance = distance; best = {target}; }
		};
		if (m_afp->loopModel().value() != static_cast<int>(Sample::Loop::Off)) { consider(sample.loopStartFrame(), Target::Loop); }
		consider(sample.startFrame(), Target::Start);
		consider(sample.endFrame(), Target::End);
		if (best.target != Target::None) { return best; }
		return {Target::View};
	}

	if (near(sample.startFrame())) { return {Target::Start}; }
	if (near(sample.endFrame())) { return {Target::End}; }

	if (padLane().contains(pos))
	{
		const auto bounds = m_afp->sliceBoundaries();
		for (std::size_t i = 0; i + 1 < bounds.size(); ++i)
		{
			if (pos.x() >= frameToX(bounds[i]) && pos.x() < frameToX(bounds[i + 1])) { return {Target::Pad, static_cast<int>(i)}; }
		}
		return {Target::View};
	}

	const auto& slices = m_afp->slices();
	int closest = -1;
	double closestDistance = GrabDistance + 1;
	for (std::size_t i = 0; i < slices.size(); ++i)
	{
		const double distance = std::abs(frameToX(slices[i] * sampleSize()) - pos.x());
		if (distance < closestDistance) { closestDistance = distance; closest = static_cast<int>(i); }
	}
	if (closest >= 0) { return {Target::Marker, closest}; }
	return {Target::View};
}




void AudioFileProcessorWaveView::updateCursor(const Hit& hit)
{
	switch (hit.target)
	{
		case Target::Start: case Target::End: case Target::Loop:
		case Target::FadeIn: case Target::FadeOut: case Target::Marker:
			setCursor(Qt::SizeHorCursor);
			break;
		case Target::Pad: setCursor(Qt::PointingHandCursor); break;
		case Target::Overview: setCursor(Qt::ArrowCursor); break;
		case Target::View: setCursor(m_drag.target == Target::View ? Qt::ClosedHandCursor : Qt::OpenHandCursor); break;
		default: setCursor(sampleSize() <= 1 ? Qt::PointingHandCursor : Qt::ArrowCursor); break;
	}
}




int AudioFileProcessorWaveView::snapped(double frame, bool bypass) const
{
	const int f = std::clamp(static_cast<int>(std::lround(frame)), 0, sampleSize());
	return m_afp->snapModel().value() && !bypass ? m_afp->nearestZeroCrossing(f) : f;
}




void AudioFileProcessorWaveView::mousePressEvent(QMouseEvent* me)
{
	const auto pos = position(me);
	if (me->button() != Qt::LeftButton) { return; }
	if (sampleSize() <= 1)
	{
		emit openFileRequested();
		return;
	}

	m_drag = hitTest(pos);
	m_pressPos = pos;
	m_lastPos = pos;
	if (m_drag.target == Target::Pad) { auditionSlice(m_drag.index); }
	else if (m_drag.target == Target::Overview) { dragTo(pos, false); }
	updateCursor(m_drag);
	update();
}




void AudioFileProcessorWaveView::mouseMoveEvent(QMouseEvent* me)
{
	const auto pos = position(me);
	if (m_drag.target == Target::None)
	{
		const auto hit = hitTest(pos);
		if (hit.target != m_hover.target || hit.index != m_hover.index)
		{
			m_hover = hit;
			update();
		}
		updateCursor(hit);
		return;
	}
	dragTo(pos, me->modifiers() & Qt::ShiftModifier);
	m_lastPos = pos;
}




void AudioFileProcessorWaveView::dragTo(const QPoint& pos, bool fine)
{
	const auto& sample = m_afp->sample();
	const double size = std::max(1, sampleSize());
	// Positions are stored as fractions; aim at the middle of the frame so truncation lands on it
	const auto toValue = [&](int frame) { return static_cast<float>((frame + 0.5) / size); };
	const int minLength = std::max(1, sample.sampleRate() / 1000);
	const int frame = snapped(xToFrame(pos.x()), fine);

	switch (m_drag.target)
	{
		case Target::Start:
			m_afp->startPointModel().setValue(toValue(std::min(frame, sample.endFrame() - minLength)));
			break;
		case Target::End:
			m_afp->endPointModel().setValue(toValue(std::max(frame, sample.startFrame() + minLength)));
			break;
		case Target::Loop:
			m_afp->loopPointModel().setValue(toValue(std::clamp(frame, sample.startFrame(), sample.endFrame() - minLength)));
			break;
		case Target::FadeIn:
		{
			const double frames = std::clamp(xToFrame(pos.x()) - sample.startFrame(), 0.0, double(sample.endFrame() - sample.startFrame()));
			m_afp->fadeInModel().setValue(static_cast<float>(frames * 1000.0 / sample.sampleRate()));
			break;
		}
		case Target::FadeOut:
		{
			const double frames = std::clamp(sample.endFrame() - xToFrame(pos.x()), 0.0, double(sample.endFrame() - sample.startFrame()));
			m_afp->fadeOutModel().setValue(static_cast<float>(frames * 1000.0 / sample.sampleRate()));
			break;
		}
		case Target::Marker:
		{
			auto slices = m_afp->slices();
			if (m_drag.index < 0 || m_drag.index >= static_cast<int>(slices.size())) { break; }
			const float value = static_cast<float>(frame / size);
			slices[m_drag.index] = value;
			m_afp->setSlices(slices);
			// Sorting may have moved the marker past a neighbour
			const auto& sorted = m_afp->slices();
			m_drag.index = static_cast<int>(std::min_element(sorted.begin(), sorted.end(),
				[value](float a, float b) { return std::abs(a - value) < std::abs(b - value); }) - sorted.begin());
			break;
		}
		case Target::Overview:
		{
			const double center = static_cast<double>(pos.x()) / std::max(1, width()) * size;
			const double span = m_viewTo - m_viewFrom;
			setView(center - span / 2, center + span / 2);
			break;
		}
		case Target::View:
		{
			const int dx = pos.x() - m_lastPos.x();
			const int dy = pos.y() - m_lastPos.y();
			// The first movement decides: sideways pans, up and down zooms around the press point
			if (m_drag.index < 0 && (std::abs(pos.x() - m_pressPos.x()) > 2 || std::abs(pos.y() - m_pressPos.y()) > 2))
			{
				m_drag.index = std::abs(pos.y() - m_pressPos.y()) > std::abs(pos.x() - m_pressPos.x()) ? 1 : 0;
			}
			if (m_drag.index == 0)
			{
				const double shift = -dx * (m_viewTo - m_viewFrom) / std::max(1, width());
				setView(m_viewFrom + shift, m_viewTo + shift);
			}
			else if (m_drag.index == 1) { zoomAt(m_pressPos.x(), std::exp(dy * 0.02)); }
			break;
		}
		default: break;
	}
	update();
}




void AudioFileProcessorWaveView::mouseReleaseEvent(QMouseEvent* me)
{
	if (m_drag.target == Target::Pad) { stopAudition(); }
	if (m_drag.target != Target::None && m_drag.target != Target::View && m_drag.target != Target::Overview
		&& m_drag.target != Target::Pad)
	{
		Engine::getSong()->setModified();
	}
	m_drag = {};
	m_hover = hitTest(position(me));
	updateCursor(m_hover);
	update();
}




void AudioFileProcessorWaveView::mouseDoubleClickEvent(QMouseEvent* me)
{
	const auto pos = position(me);
	const auto hit = hitTest(pos);
	switch (hit.target)
	{
		case Target::FadeIn: m_afp->fadeInModel().setValue(0.f); break;
		case Target::FadeOut: m_afp->fadeOutModel().setValue(0.f); break;
		case Target::Marker:
		{
			// Double-click a marker to remove it
			auto slices = m_afp->slices();
			slices.erase(slices.begin() + hit.index);
			m_afp->setSlices(slices);
			break;
		}
		case Target::View:
			if (m_afp->sliceMode() && waveRect().contains(pos))
			{
				// ...or empty space to add one
				auto slices = m_afp->slices();
				slices.push_back(static_cast<float>(snapped(xToFrame(pos.x()), me->modifiers() & Qt::ShiftModifier)
					/ static_cast<double>(sampleSize())));
				m_afp->setSlices(slices);
			}
			else { zoomToFit(); }
			break;
		case Target::Overview: zoomToFit(); break;
		default: break;
	}
	m_hover = hitTest(pos);
	update();
}




void AudioFileProcessorWaveView::wheelEvent(QWheelEvent* we)
{
	if (sampleSize() <= 1) { return; }
	const auto delta = we->angleDelta();
	const double x = we->position().x();
	if (delta.x() != 0 || we->modifiers() & Qt::ShiftModifier)
	{
		const int steps = delta.x() != 0 ? delta.x() : delta.y();
		const double shift = -steps / 120.0 * 0.15 * (m_viewTo - m_viewFrom);
		setView(m_viewFrom + shift, m_viewTo + shift);
	}
	else { zoomAt(x, std::pow(0.8, delta.y() / 120.0)); }
	we->accept();
}




void AudioFileProcessorWaveView::leaveEvent(QEvent*)
{
	if (m_drag.target == Target::None && m_hover.target != Target::None)
	{
		m_hover = {};
		update();
	}
}




void AudioFileProcessorWaveView::contextMenuEvent(QContextMenuEvent* ce)
{
	if (sampleSize() <= 1) { return; }
	const auto hit = hitTest(ce->pos());
	QMenu menu(this);
	if (hit.target == Target::Marker)
	{
		menu.addAction(tr("Remove slice"), this, [this, index = hit.index]
		{
			auto slices = m_afp->slices();
			if (index < static_cast<int>(slices.size())) { slices.erase(slices.begin() + index); }
			m_afp->setSlices(slices);
		});
	}
	if (m_afp->sliceMode())
	{
		const double frame = snapped(xToFrame(ce->pos().x()), false);
		if (hit.target != Target::Marker && waveRect().contains(ce->pos()))
		{
			menu.addAction(tr("Add slice here"), this, [this, frame]
			{
				auto slices = m_afp->slices();
				slices.push_back(static_cast<float>(frame / sampleSize()));
				m_afp->setSlices(slices);
			});
		}
		menu.addAction(tr("Copy slices as notes"), this, [this] { m_afp->copySlicesAsNotes(); });
		menu.addAction(tr("Clear slices"), this, [this] { m_afp->setSlices({}); });
	}
	else
	{
		menu.addAction(tr("Reset fades"), this, [this]
		{
			m_afp->fadeInModel().setValue(0.f);
			m_afp->fadeOutModel().setValue(0.f);
		});
	}
	menu.addAction(tr("Normalize"), this, [this] { m_afp->normalize(); });
	menu.addSeparator();
	menu.addAction(tr("Zoom to region"), this, [this]
	{
		const auto& sample = m_afp->sample();
		const double margin = (sample.endFrame() - sample.startFrame()) * 0.05;
		setView(sample.startFrame() - margin, sample.endFrame() + margin);
	});
	menu.addAction(tr("Show whole sample"), this, &AudioFileProcessorWaveView::zoomToFit);
	menu.exec(ce->globalPos());
}




void AudioFileProcessorWaveView::auditionSlice(int index)
{
	stopAudition();
	m_auditionKey = m_afp->instrumentTrack()->baseNote() + index;
	if (m_auditionKey < 0 || m_auditionKey >= NumKeys) { m_auditionKey = -1; return; }
	m_afp->instrumentTrack()->processInEvent(MidiEvent(MidiNoteOn, -1, m_auditionKey, MidiDefaultVelocity));
}




void AudioFileProcessorWaveView::stopAudition()
{
	if (m_auditionKey < 0) { return; }
	m_afp->instrumentTrack()->processInEvent(MidiEvent(MidiNoteOff, -1, m_auditionKey, 0));
	m_auditionKey = -1;
}

} // namespace gui

} // namespace lmms
