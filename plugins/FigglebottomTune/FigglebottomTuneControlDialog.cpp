/*
 * FigglebottomTuneControlDialog.cpp - editor for the FigglebottomTune pitch corrector
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

#include "FigglebottomTuneControlDialog.h"

#include <cmath>
#include <vector>

#include <QCursor>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QVBoxLayout>

#include "ComboBox.h"
#include "DeprecationHelper.h"
#include "embed.h"
#include "FontHelper.h"
#include "Knob.h"
#include "LeftRightNav.h"
#include "ModernWidgets.h"
#include "FigglebottomTuneControls.h"

namespace lmms::gui
{

namespace
{

constexpr float HistorySeconds = 4.f;
constexpr int KeyWidth = 42;
constexpr int LedColumn = 12;
constexpr float MinVisibleNotes = 12.f;
constexpr float MaxVisibleNotes = 48.f;
constexpr float LowestNote = 24.f;  // C1
constexpr float HighestNote = 96.f; // C7

bool isBlackKey(int pitchClass)
{
	return pitchClass == 1 || pitchClass == 3 || pitchClass == 6 || pitchClass == 8 || pitchClass == 10;
}

QColor withAlpha(QColor color, float alpha)
{
	color.setAlphaF(std::clamp(alpha, 0.f, 1.f));
	return color;
}

//! A column of knobs under a flat section title
QWidget* makeGroup(QWidget* parent, const QString& title, std::initializer_list<QWidget*> widgets)
{
	auto group = new QWidget(parent);
	auto layout = new QVBoxLayout(group);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(4);
	layout->addWidget(new ModernHeader(title, group));
	auto row = new QHBoxLayout();
	row->setSpacing(6);
	for (auto w : widgets) { row->addWidget(w, 0, Qt::AlignBottom); }
	row->addStretch(1);
	layout->addLayout(row);
	return group;
}

} // namespace


FigglebottomTuneControlDialog::FigglebottomTuneControlDialog(FigglebottomTuneControls* controls) :
	EffectControlDialog(controls)
{
	modern::applyWindowStyle(this);
	auto c = controls;

	auto root = new QVBoxLayout(this);
	root->setContentsMargins(8, 8, 8, 8);
	root->setSpacing(6);
	root->setSizeConstraint(QLayout::SetFixedSize);

	// Presets, key and scale
	auto top = new QHBoxLayout();
	top->setSpacing(6);
	auto preset = new ComboBox(this);
	preset->setModel(&c->m_presetModel);
	preset->setFixedWidth(170);
	preset->setToolTip(tr("Presets set the correction and output controls; the key and scale stay as they are"));
	top->addWidget(preset);
	auto nav = new LeftRightNav(this);
	nav->setToolTip(tr("Previous / next preset"));
	connect(nav, &LeftRightNav::onNavLeft, this, [c] {
		const int n = c->m_presetModel.size();
		c->m_presetModel.setValue((c->m_presetModel.value() + n - 1) % n);
	});
	connect(nav, &LeftRightNav::onNavRight, this, [c] {
		c->m_presetModel.setValue((c->m_presetModel.value() + 1) % c->m_presetModel.size());
	});
	top->addWidget(nav);
	top->addStretch(1);

	auto key = new ModernSegmented(this);
	key->setModel(&c->m_keyModel);
	key->setFont(adjustedToPixelSize(font(), SMALL_FONT_SIZE));
	// Each key's label gets a stripe in the colour its notes have in the roll
	key->setItemPainter([key, c](QPainter& p, const QRectF& r, int index, const QColor& color) {
		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(color);
		p.setFont(key->font());
		p.drawText(r.adjusted(-4, -4, 4, 2), Qt::AlignCenter, c->m_keyModel.itemText(index));
		p.setPen(Qt::NoPen);
		p.setBrush(withAlpha(PitchRoll::noteColor(index), key->model()->value() == index ? 0.95f : 0.45f));
		p.drawRoundedRect(QRectF(r.left() + 1, r.bottom() + 1, r.width() - 2, 2), 1, 1);
	}, 24);
	key->setToolTip(tr("Key: the root note of the scale"));
	top->addWidget(key);

	auto scale = new ComboBox(this);
	scale->setModel(&c->m_scaleModel);
	scale->setFixedWidth(140);
	scale->setToolTip(tr("Scale: sets which notes the voice snaps to. Click the keys in the roll to pick your own."));
	top->addWidget(scale);
	root->addLayout(top);

	auto displayRow = new QHBoxLayout();
	displayRow->setSpacing(6);
	displayRow->addWidget(new PitchRoll(c, this), 1);
	displayRow->addWidget(new ModernMeter(this, &c->m_outPeakL, &c->m_outPeakR));
	root->addLayout(displayRow);

	auto retune = modern::makeKnob(this, &c->m_retuneModel, tr("RETUNE"), tr("Retune speed:"), " ms");
	retune->setToolTip(tr("How fast the pitch is pulled onto the note. 0 ms is the hard, robotic snap."));
	auto glide = modern::makeKnob(this, &c->m_glideModel, tr("GLIDE"), tr("Note glide:"), " ms");
	glide->setToolTip(tr("How long a jump from one scale note to the next takes"));
	auto amount = modern::makeKnob(this, &c->m_amountModel, tr("AMOUNT"), tr("Amount:"), "%");
	amount->setToolTip(tr("How much of the correction is applied"));
	auto humanize = modern::makeKnob(this, &c->m_humanizeModel, tr("HUMANIZE"), tr("Humanize:"), "%");
	humanize->setToolTip(tr("Keeps the singer's vibrato on top of the corrected note"));
	auto correctionGroup = makeGroup(this, tr("CORRECTION"), {retune, glide, amount, humanize});

	auto sticky = modern::makeKnob(this, &c->m_stickyModel, tr("STICKY"), tr("Sticky:"), " cents");
	sticky->setToolTip(tr("How far past the halfway point the voice must go before it snaps to the next note"));
	auto tolerance = modern::makeKnob(this, &c->m_toleranceModel, tr("TOLERANCE"), tr("Tolerance:"), " cents");
	tolerance->setToolTip(tr("Pitch this close to the note is left alone"));
	auto tracking = modern::makeKnob(this, &c->m_trackingModel, tr("TRACKING"), tr("Tracking:"), "%");
	tracking->setToolTip(tr("Low: only clear, steady pitches are tracked. High: breathy or noisy sounds are "
		"tracked too, with more chance of glitches."));
	auto snapGroup = makeGroup(this, tr("SNAP"), {sticky, tolerance, tracking});

	auto shift = modern::makeKnob(this, &c->m_shiftModel, tr("SHIFT"), tr("Shift:"), " st");
	shift->setToolTip(tr("Transposes the corrected voice; the formants stay put"));
	auto outputGroup = makeGroup(this, tr("OUTPUT"), {shift,
		modern::makeKnob(this, &c->m_fineModel, tr("FINE"), tr("Fine tune:"), " cents"),
		modern::makeKnob(this, &c->m_mixModel, tr("MIX"), tr("Mix:"), "%"),
		modern::makeKnob(this, &c->m_gainModel, tr("GAIN"), tr("Output gain:"), " dB")});

	auto sections = new QHBoxLayout();
	sections->setSpacing(14);
	sections->addWidget(correctionGroup);
	sections->addWidget(snapGroup);
	sections->addWidget(outputGroup);
	sections->addStretch(1);
	auto logo = new QLabel(this);
	logo->setPixmap(PLUGIN_NAME::getIconPixmap("logo"));
	logo->setToolTip(tr("Figglebottom"));
	sections->addWidget(logo, 0, Qt::AlignBottom | Qt::AlignRight);
	root->addLayout(sections);
}




PitchRoll::PitchRoll(FigglebottomTuneControls* controls, QWidget* parent) :
	QWidget(parent),
	m_controls(controls)
{
	setMinimumSize(sizeHint());
	setMouseTracking(true);
	setToolTip(tr("Click or drag over the keys to switch notes on and off. Scroll or drag to move up and down, "
		"Ctrl+scroll to zoom, double-click to follow the voice again."));
	m_followTarget = m_bottomNote;
	m_sinceNewData.start();

	for (int i = 0; i < figglebottomtune::PitchClasses; ++i)
	{
		connect(controls->m_noteModels[i], &Model::dataChanged, this, qOverload<>(&QWidget::update));
	}

	auto timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, [this] { if (isVisible()) { tick(); } });
	timer->start(33);
}


QColor PitchRoll::noteColor(int pitchClass, float saturation, float value)
{
	return QColor::fromHsvF(figglebottomtune::pitchClass(pitchClass) / 12.f, saturation, value);
}


QRectF PitchRoll::keysRect() const
{
	return QRectF(1, 1, KeyWidth, height() - 2);
}


QRectF PitchRoll::plotRect() const
{
	return QRectF(rect()).adjusted(KeyWidth + LedColumn + 1, 1, -1, -1);
}


float PitchRoll::yForNote(float note) const
{
	const QRectF plot = plotRect();
	return plot.bottom() - (note - m_bottomNote + 0.5f) * plot.height() / m_visibleNotes;
}


float PitchRoll::noteAt(float y) const
{
	const QRectF plot = plotRect();
	return m_bottomNote - 0.5f + (plot.bottom() - y) * m_visibleNotes / plot.height();
}


void PitchRoll::tick()
{
	const auto& trace = m_controls->m_trace;
	const auto count = trace.count();
	if (count != m_lastCount)
	{
		m_lastCount = count;
		m_sinceNewData.restart();
	}
	const bool live = m_sinceNewData.elapsed() < 200;

	for (auto& glow : m_glow) { glow *= 0.88f; }
	if (live && count > 0)
	{
		const auto newest = trace.at(count - 1);
		if (newest.target >= 0) { m_glow[figglebottomtune::pitchClass(newest.target)] = 1.f; }
	}

	// Follow the voice, but only move once it nears the edge so the roll isn't constantly drifting
	if (m_follow && live)
	{
		const auto hops = std::min<std::uint64_t>(count, static_cast<std::uint64_t>(0.4f / trace.hopSeconds()));
		float sum = 0.f;
		int voiced = 0;
		for (std::uint64_t i = count - hops; i < count; ++i)
		{
			const float note = trace.at(i).output;
			if (!std::isnan(note)) { sum += note; ++voiced; }
		}
		if (voiced > 0)
		{
			const float note = sum / voiced;
			if (note < m_bottomNote + m_visibleNotes * 0.2f || note > m_bottomNote + m_visibleNotes * 0.8f)
			{
				m_followTarget = note - m_visibleNotes * 0.5f;
			}
		}
	}
	// Hold still while the mouse is on the keys, so they don't slide away from under a click
	const bool onKeys = underMouse() && mapFromGlobal(QCursor::pos()).x() < plotRect().left();
	if (m_follow && !onKeys)
	{
		m_followTarget = std::clamp(m_followTarget, LowestNote, HighestNote - m_visibleNotes);
		m_bottomNote += (m_followTarget - m_bottomNote) * 0.12f;
	}
	update();
}


void PitchRoll::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	const QRectF area = QRectF(rect());
	modern::paintDisplay(p, area, 0, 0);
	p.setRenderHint(QPainter::Antialiasing);
	const QRectF plot = plotRect();
	const auto c = m_controls;
	const auto& trace = c->m_trace;
	const float rowHeight = plot.height() / m_visibleNotes;

	QFont small = adjustedToPixelSize(font(), SMALL_FONT_SIZE);
	p.setFont(small);
	const auto fm = QFontMetrics(small);

	const auto count = trace.count();
	const float hop = trace.hopSeconds();
	const bool live = m_sinceNewData.elapsed() < 200;
	const auto newest = count > 0 ? trace.at(count - 1) : PitchTrace::Entry{NAN, NAN, -1, -120.f};
	const int activeTarget = live && !std::isnan(newest.output) ? newest.target : -1000;

	// Note lanes: scale notes tinted in their colour, the rest dimmed, black keys a shade darker
	p.save();
	p.setClipRect(plot);
	const int lowest = static_cast<int>(std::floor(m_bottomNote - 0.5f));
	const int highest = static_cast<int>(std::ceil(m_bottomNote + m_visibleNotes));
	for (int n = lowest; n <= highest; ++n)
	{
		const int pc = figglebottomtune::pitchClass(n);
		const QRectF row(plot.left(), yForNote(n + 0.5f), plot.width(), rowHeight);
		if (isBlackKey(pc)) { p.fillRect(row, QColor(0, 0, 0, 40)); }
		if (c->noteEnabled(pc))
		{
			const float glow = n == activeTarget ? m_glow[pc] : m_glow[pc] * 0.3f;
			p.fillRect(row, withAlpha(noteColor(pc), 0.06f + 0.16f * glow));
		}
		else { p.fillRect(row, QColor(0, 0, 0, 70)); }
		p.setPen(QPen(pc == 0 ? modern::gridColor() : QColor(255, 255, 255, 9), 1));
		p.drawLine(QPointF(plot.left(), row.bottom()), QPointF(plot.right(), row.bottom()));
	}

	// The roll keeps scrolling for a moment after the audio stops, so old traces slide out
	const float idle = std::max(0.f, (m_sinceNewData.elapsed() - 150) * 0.001f);
	const float pixelsPerSecond = static_cast<float>(plot.width()) / HistorySeconds;
	const float newestTime = count * hop;
	const auto xFor = [&](double time) {
		return plot.right() - (newestTime + idle - time) * pixelsPerSecond;
	};

	// Beat-like time grid, half a second apart, moving with the audio
	p.setPen(QPen(QColor(255, 255, 255, 10), 1));
	for (double t = std::floor((newestTime + idle) * 2.0) * 0.5; xFor(t) > plot.left(); t -= 0.5)
	{
		p.drawLine(QPointF(xFor(t), plot.top()), QPointF(xFor(t), plot.bottom()));
	}

	// Traces, split wherever the voice stops or snaps to a different note
	struct Segment
	{
		int target;
		std::vector<QPointF> input;
		std::vector<QPointF> output;
		float level = 0.f;
	};
	std::vector<Segment> segments;
	const auto hops = std::min<std::uint64_t>(count, static_cast<std::uint64_t>(
		(HistorySeconds + idle) / hop) + 2);
	bool open = false;
	for (std::uint64_t i = count - hops; i < count; ++i)
	{
		const auto e = trace.at(i);
		if (std::isnan(e.input) || std::isnan(e.output)) { open = false; continue; }
		const double x = xFor(i * static_cast<double>(hop));
		const QPointF in(x, yForNote(e.input));
		const QPointF out(x, yForNote(e.output));
		if (!open || segments.back().target != e.target)
		{
			Segment next{e.target, {}, {}, 0.f};
			// Joined to the previous note's last point, so a snap draws as a step in the line
			if (open)
			{
				next.input.push_back(segments.back().input.back());
				next.output.push_back(segments.back().output.back());
			}
			segments.push_back(std::move(next));
			open = true;
		}
		auto& s = segments.back();
		s.input.push_back(in);
		s.output.push_back(out);
		s.level = std::max(s.level, e.level);
	}

	const auto polyline = [](const std::vector<QPointF>& points) {
		QPainterPath path;
		path.moveTo(points.front());
		for (std::size_t i = 1; i < points.size(); ++i) { path.lineTo(points[i]); }
		return path;
	};
	for (const auto& s : segments)
	{
		const QColor color = s.target >= 0 ? noteColor(s.target, 0.55f) : modern::textColor();
		// Quiet passages fade back a little
		const float presence = std::clamp((s.level + 50.f) / 30.f, 0.45f, 1.f);

		// The correction itself: the band between what was sung and what comes out
		QPainterPath band = polyline(s.output);
		for (auto it = s.input.rbegin(); it != s.input.rend(); ++it) { band.lineTo(*it); }
		band.closeSubpath();
		p.fillPath(band, withAlpha(color, 0.16f * presence));

		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(QColor(255, 255, 255, static_cast<int>(120 * presence)), 1.1, Qt::SolidLine,
			Qt::RoundCap, Qt::RoundJoin));
		p.drawPath(polyline(s.input));

		const QPainterPath out = polyline(s.output);
		p.setPen(QPen(withAlpha(color, 0.18f * presence), 6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		p.drawPath(out);
		p.setPen(QPen(withAlpha(color, presence), 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		p.drawPath(out);
	}

	// Playhead dots: where the voice is now and where it's being put
	if (live && !std::isnan(newest.output))
	{
		const QColor color = newest.target >= 0 ? noteColor(newest.target, 0.55f) : modern::textColor();
		const QPointF in(plot.right() - 4, yForNote(newest.input));
		const QPointF out(plot.right() - 4, yForNote(newest.output));
		p.setPen(QPen(QColor(255, 255, 255, 90), 1, Qt::DotLine));
		p.drawLine(in, out);
		p.setPen(Qt::NoPen);
		p.setBrush(withAlpha(color, 0.25f));
		p.drawEllipse(out, 7, 7);
		p.setBrush(color);
		p.drawEllipse(out, 3.5, 3.5);
		p.setPen(QPen(QColor(255, 255, 255, 200), 1.2));
		p.setBrush(Qt::NoBrush);
		p.drawEllipse(in, 2.5, 2.5);
	}
	p.restore();

	paintKeys(p, activeTarget);

	// Legend, top left
	{
		const QPointF at(plot.left() + 8, plot.top() + 4 + fm.ascent());
		p.setPen(QPen(QColor(255, 255, 255, 150), 1.1));
		p.drawLine(at + QPointF(0, -fm.capHeight() / 2.0), at + QPointF(12, -fm.capHeight() / 2.0));
		p.setPen(modern::dimTextColor());
		p.drawText(at + QPointF(16, 0), tr("in"));
		const float x = 16 + fm.horizontalAdvance(tr("in")) + 10;
		p.setPen(QPen(noteColor(static_cast<int>(m_bottomNote + m_visibleNotes * 0.5f), 0.55f), 2.2,
			Qt::SolidLine, Qt::RoundCap));
		p.drawLine(at + QPointF(x, -fm.capHeight() / 2.0), at + QPointF(x + 12, -fm.capHeight() / 2.0));
		p.setPen(modern::dimTextColor());
		p.drawText(at + QPointF(x + 16, 0), tr("out"));
	}

	// Readout, top right: the sung note, how far off it is, and where it's going
	QString text;
	QColor textColor = modern::dimTextColor();
	if (live && !std::isnan(newest.input) && newest.target >= 0)
	{
		const int cents = static_cast<int>(std::lround((newest.input - newest.target) * 100.f));
		text = tr("%1 %2%3¢  →  %4").arg(FigglebottomTuneControls::noteName(static_cast<int>(std::lround(newest.input))))
			.arg(cents > 0 ? "+" : cents < 0 ? "−" : "±").arg(std::abs(cents))
			.arg(FigglebottomTuneControls::noteName(newest.target));
		textColor = noteColor(newest.target, 0.4f);
	}
	else if (live && !std::isnan(newest.input))
	{
		text = tr("%1  (no notes on)").arg(FigglebottomTuneControls::noteName(static_cast<int>(std::lround(newest.input))));
	}
	else { text = live ? tr("listening…") : tr("waiting for audio"); }
	const float shiftSemis = c->m_shiftModel.value() + c->m_fineModel.value() * 0.01f;
	if (std::abs(shiftSemis) >= 0.005f)
	{
		text += tr("   shift %1%2").arg(shiftSemis > 0 ? "+" : "").arg(shiftSemis, 0, 'f', 2);
	}
	if (!m_follow) { text += tr("   · follow off"); }
	const QRectF box(plot.right() - 10 - fm.horizontalAdvance(text), plot.top() + 3,
		fm.horizontalAdvance(text) + 8, fm.height() + 2);
	p.fillRect(box, withAlpha(modern::displayColor(), 0.8f));
	p.setPen(textColor);
	p.drawText(box, Qt::AlignCenter, text);
}


void PitchRoll::paintKeys(QPainter& p, int activeTarget)
{
	const QRectF keys = keysRect();
	const float rowHeight = static_cast<float>(plotRect().height()) / m_visibleNotes;
	const QColor white(208, 212, 219);
	const QColor black(30, 33, 38);

	p.save();
	p.setClipRect(keys.adjusted(0, 0, LedColumn, 0));
	p.fillRect(keys, white);
	const int lowest = static_cast<int>(std::floor(m_bottomNote - 0.5f));
	const int highest = static_cast<int>(std::ceil(m_bottomNote + m_visibleNotes));
	for (int n = lowest; n <= highest; ++n)
	{
		const int pc = figglebottomtune::pitchClass(n);
		const bool enabled = m_controls->noteEnabled(pc);
		const QRectF row(keys.left(), yForNote(n + 0.5f), keys.width(), rowHeight);
		const QColor color = noteColor(pc);

		// Equal-height rows like the piano roll: black keys are short bars over the white ones,
		// and white keys meet in the middle of a black key except at E-F and B-C
		if (isBlackKey(pc))
		{
			p.setPen(QPen(QColor(0, 0, 0, 90), 1));
			p.drawLine(QPointF(row.left(), row.center().y()), QPointF(row.right(), row.center().y()));
			const QRectF bar(row.left(), row.top(), row.width() * 0.62, row.height());
			p.fillRect(bar, n == activeTarget ? color.darker(130) : black);
		}
		else
		{
			if (pc == 0 || pc == 5)
			{
				p.setPen(QPen(QColor(0, 0, 0, 110), 1));
				p.drawLine(row.bottomLeft(), row.bottomRight());
			}
			if (n == activeTarget) { p.fillRect(row.adjusted(0, 0.5, 0, -0.5), color); }
			else if (!enabled) { p.fillRect(row, QColor(0, 0, 0, 45)); }
		}

		if (pc == 0 && rowHeight >= 6.f)
		{
			p.setPen(QColor(60, 64, 72));
			p.drawText(row.adjusted(0, -4, -3, 4), Qt::AlignRight | Qt::AlignVCenter,
				FigglebottomTuneControls::noteName(n));
		}

		// Scale LED column between the keys and the roll
		const float size = std::clamp(rowHeight - 3.f, 3.f, 7.f);
		const QRectF led(keys.right() + (LedColumn - size) / 2.f, row.center().y() - size / 2.f, size, size);
		if (enabled)
		{
			p.setPen(Qt::NoPen);
			p.setBrush(n == activeTarget ? color.lighter(115) : withAlpha(color, 0.8f));
			p.drawRoundedRect(led, 1.5, 1.5);
		}
		else
		{
			p.setPen(QPen(withAlpha(modern::dimTextColor(), 0.35f), 1));
			p.setBrush(Qt::NoBrush);
			p.drawRoundedRect(led.adjusted(0.5, 0.5, -0.5, -0.5), 1.5, 1.5);
		}
	}
	p.setPen(QPen(QColor(0, 0, 0, 160), 1));
	p.drawLine(keys.topRight(), keys.bottomRight());
	p.restore();
}


void PitchRoll::mousePressEvent(QMouseEvent* me)
{
	if (me->button() != Qt::LeftButton) { return; }
	const QPointF pos = positionF(me);
	const int note = static_cast<int>(std::lround(noteAt(static_cast<float>(pos.y()))));
	if (pos.x() < plotRect().left())
	{
		const int pc = figglebottomtune::pitchClass(note);
		m_paintingKeys = m_controls->noteEnabled(pc) ? 0 : 1;
		m_lastPaintedNote = note;
		m_controls->setNote(pc, m_paintingKeys == 1);
		return;
	}
	m_dragging = true;
	m_dragStartY = static_cast<float>(pos.y());
	m_dragStartBottom = m_bottomNote;
	setCursor(Qt::ClosedHandCursor);
}


void PitchRoll::mouseMoveEvent(QMouseEvent* me)
{
	const QPointF pos = positionF(me);
	if (m_paintingKeys >= 0)
	{
		const int note = static_cast<int>(std::lround(noteAt(static_cast<float>(pos.y()))));
		if (note != m_lastPaintedNote)
		{
			m_lastPaintedNote = note;
			m_controls->setNote(figglebottomtune::pitchClass(note), m_paintingKeys == 1);
		}
		return;
	}
	if (m_dragging)
	{
		const float notesPerPixel = m_visibleNotes / static_cast<float>(plotRect().height());
		m_follow = false;
		m_bottomNote = std::clamp(m_dragStartBottom + (static_cast<float>(pos.y()) - m_dragStartY) * notesPerPixel,
			LowestNote, HighestNote - m_visibleNotes);
		update();
		return;
	}
	setCursor(pos.x() < plotRect().left() ? Qt::PointingHandCursor : Qt::OpenHandCursor);
}


void PitchRoll::mouseReleaseEvent(QMouseEvent*)
{
	m_paintingKeys = -1;
	m_dragging = false;
	unsetCursor();
}


void PitchRoll::mouseDoubleClickEvent(QMouseEvent* me)
{
	// On the keys a double-click is just two clicks; on the roll it brings back following
	if (position(me).x() < plotRect().left())
	{
		mousePressEvent(me);
		return;
	}
	m_follow = true;
	update();
}


void PitchRoll::wheelEvent(QWheelEvent* we)
{
	we->accept();
	const int steps = m_wheelSteps.steps(we);
	if (steps == 0) { return; }
	if (we->modifiers() & Qt::ControlModifier)
	{
		// Zoom around the note under the cursor
		const float anchor = noteAt(static_cast<float>(we->position().y()));
		const float visible = std::clamp(m_visibleNotes - steps * 2.f, MinVisibleNotes, MaxVisibleNotes);
		m_bottomNote += (anchor - m_bottomNote) * (1.f - visible / m_visibleNotes);
		m_visibleNotes = visible;
	}
	else
	{
		m_follow = false;
		m_bottomNote += static_cast<float>(steps);
	}
	m_bottomNote = std::clamp(m_bottomNote, LowestNote, HighestNote - m_visibleNotes);
	m_followTarget = m_bottomNote;
	update();
}

} // namespace lmms::gui
