/*
 * InstrumentFunctionViews.cpp - view for instrument-functions-tab
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

#include <QGridLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <climits>
#include <random>

#include "InstrumentFunctions.h"
#include "InstrumentFunctionViews.h"
#include "ComboBox.h"
#include "FontHelper.h"
#include "Knob.h"
#include "ModernWidgets.h"
#include "TempoSyncKnob.h"


namespace lmms::gui
{

namespace
{

using Chord = InstrumentFunctionNoteStacking::Chord;

const Chord& chordAt(int index)
{
	const auto& chords = InstrumentFunctionNoteStacking::ChordTable::getInstance().chords();
	return chords[std::clamp<std::size_t>(index, 0, chords.size() - 1)];
}

QString noteName(int semitones)
{
	static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
	return names[((semitones % 12) + 12) % 12];
}

void paintChip(QPainter& p, const QFont& font, const QPointF& anchor, Qt::Alignment align, const QString& text,
	const QColor& color)
{
	p.setFont(font);
	const auto fm = QFontMetrics(font);
	const qreal w = fm.horizontalAdvance(text) + 8, h = fm.capHeight() + 7;
	const qreal x = align & Qt::AlignRight ? anchor.x() - w : anchor.x();
	const qreal y = align & Qt::AlignBottom ? anchor.y() - h : anchor.y();
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(0, 0, 0, 170));
	p.drawRect(QRectF(x, y, w, h));
	p.setPen(color);
	p.drawText(QPointF(x + 4, y + 3.5 + fm.capHeight()), text);
}

//! Glyphs for the arpeggio directions: up, down, up and down, down and up, random
void paintDirection(QPainter& p, const QRectF& r, int direction, const QColor& color)
{
	static constexpr qreal levels[5][5] = {
		{1.0, 0.75, 0.5, 0.25, 0.0},
		{0.0, 0.25, 0.5, 0.75, 1.0},
		{1.0, 0.5, 0.0, 0.5, 1.0},
		{0.0, 0.5, 1.0, 0.5, 0.0},
		{0.6, 0.0, 0.9, 0.3, 0.75}};
	const auto& level = levels[std::clamp(direction, 0, 4)];
	p.setRenderHint(QPainter::Antialiasing);
	const qreal w = std::min(r.width(), 26.0);
	const qreal left = r.center().x() - w / 2;
	const qreal step = w / 5;
	if (direction != 4)
	{
		QColor line = color;
		line.setAlpha(110);
		p.setPen(QPen(line, 1));
		QPolygonF poly;
		for (int i = 0; i < 5; ++i) { poly << QPointF(left + step * (i + 0.5), r.top() + 1 + level[i] * (r.height() - 3)); }
		p.drawPolyline(poly);
	}
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	for (int i = 0; i < 5; ++i)
	{
		p.drawRect(QRectF(left + step * i + 1, r.top() + level[i] * (r.height() - 3), step - 2, 2.5));
	}
}

} // namespace


//! Mini keyboard that lights up the keys a chord stack adds, starting from C
class ChordPreview : public QWidget
{
public:
	ChordPreview(QWidget* parent) : QWidget(parent)
	{
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		setFixedHeight(18);
	}

	void setChord(const std::vector<int>& keys, int octaves, bool enabled)
	{
		m_keys = keys;
		m_octaves = std::max(1, octaves);
		m_enabled = enabled;
		update();
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter p(this);
		modern::paintDisplay(p, rect(), 0, 0);

		const QRectF area = QRectF(rect()).adjusted(3, 3, -3, -3);
		const int whiteKeys = m_octaves * 7;
		const qreal whiteWidth = area.width() / whiteKeys;
		static constexpr int whiteIndex[] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};
		auto isLit = [this](int key) { return std::find(m_keys.begin(), m_keys.end(), key) != m_keys.end(); };
		QColor accent = modern::accentColor();
		if (!m_enabled) { accent.setAlpha(90); }

		p.setPen(Qt::NoPen);
		for (int key = 0; key < m_octaves * 12; ++key)
		{
			const int white = whiteIndex[key % 12];
			if (white < 0) { continue; }
			const qreal x = area.left() + ((key / 12) * 7 + white) * whiteWidth;
			const QColor base = key == 0 ? modern::textColor() : accent;
			p.setBrush(isLit(key) ? base : QColor(58, 64, 72));
			p.drawRect(QRectF(x + 0.5, area.top(), std::max(1.0, whiteWidth - 1), area.height()));
		}
		for (int key = 0; key < m_octaves * 12; ++key)
		{
			if (whiteIndex[key % 12] >= 0) { continue; }
			const qreal x = area.left() + ((key / 12) * 7 + whiteIndex[key % 12 - 1] + 1) * whiteWidth;
			p.setBrush(isLit(key) ? accent.darker(130) : modern::displayColor());
			p.drawRect(QRectF(x - whiteWidth * 0.32, area.top(), whiteWidth * 0.64, area.height() * 0.58));
		}
	}

private:
	std::vector<int> m_keys;
	int m_octaves = 1;
	bool m_enabled = false;
};


//! Piano roll sketch of the pattern the arpeggiator plays for one held C
class ArpeggioPreview : public QWidget
{
public:
	struct Pattern
	{
		std::vector<int> keys; //!< key offset per step, or INT_MIN for a step that plays nothing
		float gate = 1.f;      //!< note length relative to the step
		float playChance = 1.f;
		QString info;          //!< shown in the top right corner
		QString time;          //!< shown in the bottom right corner
		bool enabled = false;
	};

	ArpeggioPreview(QWidget* parent) : QWidget(parent)
	{
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
		setMinimumHeight(40);
	}

	void setPattern(Pattern pattern)
	{
		m_pattern = std::move(pattern);
		update();
	}

	QSize sizeHint() const override { return QSize(240, 56); }

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const auto& keys = m_pattern.keys;
		const int steps = static_cast<int>(keys.size());
		modern::paintDisplay(p, rect(), std::max(1, steps / 4), 0);
		if (steps == 0) { return; }

		int low = INT_MAX, high = INT_MIN;
		for (int key : keys)
		{
			if (key == INT_MIN) { continue; }
			low = std::min(low, key);
			high = std::max(high, key);
		}
		if (low == INT_MAX) { return; }

		const QRectF area = QRectF(rect()).adjusted(4, 6, -4, -6);
		const qreal stepWidth = area.width() / steps;
		const int span = high - low;
		const qreal barHeight = std::clamp(area.height() / (span + 1), 2.0, 5.0);
		const qreal travel = area.height() - barHeight;

		QColor accent = modern::accentColor();
		accent.setAlphaF((m_pattern.enabled ? 1.0 : 0.35) * (0.35 + 0.65 * m_pattern.playChance));
		p.setPen(Qt::NoPen);
		p.setBrush(accent);
		for (int i = 0; i < steps; ++i)
		{
			if (keys[i] == INT_MIN) { continue; }
			const qreal y = area.top() + (span == 0 ? travel / 2 : travel * (high - keys[i]) / span);
			const qreal w = std::max(2.0, stepWidth * m_pattern.gate - 1);
			p.drawRect(QRectF(area.left() + i * stepWidth, y, std::min(w, area.right() - area.left() - i * stepWidth), barHeight));
		}

		const auto f = adjustedToPixelSize(font(), SMALL_FONT_SIZE);
		if (!m_pattern.info.isEmpty())
		{
			paintChip(p, f, QPointF(width() - 3, 3), Qt::AlignRight | Qt::AlignTop, m_pattern.info, modern::dimTextColor());
		}
		if (!m_pattern.time.isEmpty())
		{
			paintChip(p, f, QPointF(width() - 3, height() - 3), Qt::AlignRight | Qt::AlignBottom, m_pattern.time,
				modern::dimTextColor());
		}
	}

private:
	Pattern m_pattern;
};




InstrumentFunctionNoteStackingView::InstrumentFunctionNoteStackingView( InstrumentFunctionNoteStacking* cc, QWidget* parent ) :
	QWidget( parent ),
	ModelView( nullptr, this ),
	m_cc( cc ),
	m_chordsToggle(new ModernHeader(tr("STACKING"), this, true)),
	m_chordsComboBox( new ComboBox() ),
	m_chordRangeKnob(new Knob(KnobType::Modern, tr("RANGE"), this)),
	m_preview(new ChordPreview(this))
{
	auto mainLayout = new QGridLayout(this);
	mainLayout->setContentsMargins(0, 0, 0, 0);
	mainLayout->setColumnStretch(0, 1);
	mainLayout->setHorizontalSpacing(8);
	mainLayout->setVerticalSpacing(3);

	m_chordsToggle->setToolTip(tr("Play every note as a chord"));
	m_chordsComboBox->setToolTip(tr("The chord built on each note you play"));
	m_chordRangeKnob->setHintText( tr( "Chord range:" ), " " + tr( "octave(s)" ) );
	m_chordRangeKnob->setToolTip(tr("Over how many octaves the chord is stacked"));

	mainLayout->addWidget(m_chordsToggle, 0, 0, 1, 2);
	mainLayout->addWidget(m_chordsComboBox, 1, 0);
	mainLayout->addWidget(m_preview, 2, 0);
	mainLayout->addWidget(m_chordRangeKnob, 1, 1, 2, 1, Qt::AlignCenter);
}




InstrumentFunctionNoteStackingView::~InstrumentFunctionNoteStackingView() = default;




void InstrumentFunctionNoteStackingView::modelChanged()
{
	if (m_cc)
	{
		m_cc->m_chordsEnabledModel.disconnect(this);
		m_cc->m_chordsModel.disconnect(this);
		m_cc->m_chordRangeModel.disconnect(this);
	}
	m_cc = castModel<InstrumentFunctionNoteStacking>();
	m_chordsToggle->setModel( &m_cc->m_chordsEnabledModel );
	m_chordsComboBox->setModel( &m_cc->m_chordsModel );
	m_chordRangeKnob->setModel( &m_cc->m_chordRangeModel );

	for (Model* model : std::initializer_list<Model*>{&m_cc->m_chordsEnabledModel, &m_cc->m_chordsModel, &m_cc->m_chordRangeModel})
	{
		connect(model, &Model::dataChanged, this, &InstrumentFunctionNoteStackingView::updatePreview);
	}
	updatePreview();
}




void InstrumentFunctionNoteStackingView::updatePreview()
{
	// Same notes as InstrumentFunctionNoteStacking::processNote() adds
	const auto& chord = chordAt(m_cc->m_chordsModel.value());
	const int range = static_cast<int>(m_cc->m_chordRangeModel.value());
	std::vector<int> keys;
	QStringList names;
	for (int octave = 0; octave < range; ++octave)
	{
		for (int i = 0; i < chord.size(); ++i)
		{
			keys.push_back(octave * 12 + chord[i]);
			if (octave == 0) { names << noteName(chord[i]); }
		}
	}
	const int highest = keys.empty() ? 0 : *std::max_element(keys.begin(), keys.end());
	m_preview->setChord(keys, highest / 12 + 1, m_cc->m_chordsEnabledModel.value());
	m_preview->setToolTip(tr("%1 on C: %2").arg(chord.getName(), names.join(" · ")));
}







InstrumentFunctionArpeggioView::InstrumentFunctionArpeggioView( InstrumentFunctionArpeggio* arp, QWidget* parent ) :
	QWidget( parent ),
	ModelView( nullptr, this ),
	m_a( arp ),
	m_arpToggle(new ModernHeader(tr("ARPEGGIO"), this, true)),
	m_preview(new ArpeggioPreview(this)),
	m_arpComboBox( new ComboBox() ),
	m_arpRangeKnob(new Knob(KnobType::Modern, tr("RANGE"), this)),
	m_arpRepeatsKnob(new Knob(KnobType::Modern, tr("REP"), this)),
	m_arpCycleKnob(new Knob(KnobType::Modern, tr("CYCLE"), this)),
	m_arpSkipKnob(new Knob(KnobType::Modern, tr("SKIP"), this)),
	m_arpMissKnob(new Knob(KnobType::Modern, tr("MISS"), this)),
	m_arpTimeKnob(new TempoSyncKnob(KnobType::Modern, tr("TIME"), this)),
	m_arpGateKnob(new Knob(KnobType::Modern, tr("GATE"), this)),
	m_arpDirectionSelector(new ModernSegmented(this)),
	m_arpModeSelector(new ModernSegmented(this))
{
	auto mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(0, 0, 0, 0);
	mainLayout->setSpacing(3);

	m_arpToggle->setToolTip(tr("Play held notes as an arpeggio"));
	mainLayout->addWidget(m_arpToggle);

	auto chordAndModeLayout = new QHBoxLayout();
	chordAndModeLayout->setSpacing(6);
	m_arpComboBox->setToolTip(tr("The chord whose notes the arpeggio walks through"));
	chordAndModeLayout->addWidget(m_arpComboBox, 1);
	m_arpModeSelector->setToolTips({
		tr("Free: every note runs its own arpeggio"),
		tr("Sort: all held notes form one arpeggio, from low to high"),
		tr("Sync: all held notes step together")});
	chordAndModeLayout->addWidget(m_arpModeSelector);
	mainLayout->addLayout(chordAndModeLayout);

	mainLayout->addWidget(m_preview, 1);

	m_arpDirectionSelector->setLabels({tr("Up"), tr("Down"), tr("Up and down"), tr("Down and up"), tr("Random")});
	m_arpDirectionSelector->setToolTips({tr("Up"), tr("Down"), tr("Up and down"), tr("Down and up"), tr("Random")});
	m_arpDirectionSelector->setItemPainter(paintDirection, 34);
	mainLayout->addWidget(m_arpDirectionSelector);

	m_arpRangeKnob->setHintText( tr( "Arpeggio range:" ), " " + tr( "octave(s)" ) );
	m_arpRepeatsKnob->setHintText( tr( "Note repeats:" ) + " ", " " + tr( "time(s)" ) );
	m_arpCycleKnob->setHintText( tr( "Cycle notes:" ) + " ", " " + tr( "note(s)" ) );
	m_arpSkipKnob->setHintText( tr( "Skip rate:" ), tr( "%" ) );
	m_arpMissKnob->setHintText( tr( "Miss rate:" ), tr( "%" ) );
	m_arpTimeKnob->setHintText( tr( "Arpeggio time:" ), " " + tr( "ms" ) );
	m_arpGateKnob->setHintText( tr( "Arpeggio gate:" ), tr( "%" ) );

	m_arpRangeKnob->setToolTip(tr("Over how many octaves the arpeggio runs"));
	m_arpRepeatsKnob->setToolTip(tr("How often each note is played before the next"));
	m_arpCycleKnob->setToolTip(tr("Jump over this many notes on every step"));
	m_arpSkipKnob->setToolTip(tr("Chance that a step stays silent"));
	m_arpMissKnob->setToolTip(tr("Chance that a step plays a random note of the chord"));
	m_arpGateKnob->setToolTip(tr("Note length relative to the step"));
	m_arpTimeKnob->setToolTip(tr("Length of a step (right-click to sync to tempo)"));

	auto knobsLayout = new QHBoxLayout();
	knobsLayout->setSpacing(0);
	const std::initializer_list<Knob*> knobs = {m_arpRangeKnob, m_arpRepeatsKnob, m_arpCycleKnob,
		m_arpSkipKnob, m_arpMissKnob, m_arpGateKnob, m_arpTimeKnob};
	for (auto knob : knobs)
	{
		if (knob != m_arpRangeKnob) { knobsLayout->addStretch(1); }
		knobsLayout->addWidget(knob);
	}
	mainLayout->addLayout(knobsLayout);
}




InstrumentFunctionArpeggioView::~InstrumentFunctionArpeggioView() = default;




void InstrumentFunctionArpeggioView::modelChanged()
{
	const auto patternModels = [this]() {
		return std::array<Model*, 11>{&m_a->m_arpEnabledModel, &m_a->m_arpModel, &m_a->m_arpRangeModel,
			&m_a->m_arpRepeatsModel, &m_a->m_arpCycleModel, &m_a->m_arpSkipModel, &m_a->m_arpMissModel,
			&m_a->m_arpTimeModel, &m_a->m_arpGateModel, &m_a->m_arpDirectionModel, &m_a->m_arpModeModel};
	};
	if (m_a)
	{
		for (Model* model : patternModels()) { model->disconnect(this); }
	}

	m_a = castModel<InstrumentFunctionArpeggio>();
	m_arpToggle->setModel( &m_a->m_arpEnabledModel );
	m_arpComboBox->setModel( &m_a->m_arpModel );
	m_arpRangeKnob->setModel( &m_a->m_arpRangeModel );
	m_arpRepeatsKnob->setModel( &m_a->m_arpRepeatsModel );
	m_arpCycleKnob->setModel( &m_a->m_arpCycleModel );
	m_arpSkipKnob->setModel( &m_a->m_arpSkipModel );
	m_arpMissKnob->setModel( &m_a->m_arpMissModel );
	m_arpTimeKnob->setModel( &m_a->m_arpTimeModel );
	m_arpGateKnob->setModel( &m_a->m_arpGateModel );
	m_arpDirectionSelector->setModel( &m_a->m_arpDirectionModel );
	m_arpModeSelector->setModel( &m_a->m_arpModeModel );

	for (Model* model : patternModels())
	{
		connect(model, &Model::dataChanged, this, &InstrumentFunctionArpeggioView::updatePreview);
	}
	updatePreview();
}




void InstrumentFunctionArpeggioView::updatePreview()
{
	using Direction = InstrumentFunctionArpeggio::ArpDirection;
	const auto& chord = chordAt(m_a->m_arpModel.value());
	const int chordSize = std::max(1, chord.size());
	const float repeats = m_a->m_arpRepeatsModel.value();
	const float cycle = m_a->m_arpCycleModel.value();
	const auto direction = static_cast<Direction>(m_a->m_arpDirectionModel.value());
	// Same note count as InstrumentFunctionArpeggio::processNote() in free mode
	const int range = static_cast<int>(chordSize * m_a->m_arpRangeModel.value() * repeats);

	// Show two runs of the pattern, but no fewer than 8 and no more than 32 steps
	const int period = direction == Direction::UpAndDown || direction == Direction::DownAndUp
		? std::max(1, range * 2 - 2 * static_cast<int>(repeats))
		: range;
	const int steps = std::clamp(period * 2, 8, 32);

	ArpeggioPreview::Pattern pattern;
	std::minstd_rand random(1234); // fixed seed, so the random pattern does not flicker
	for (int step = 0; step < steps; ++step)
	{
		const int index = direction == Direction::Random
			? static_cast<int>(static_cast<int>(random() % std::max(1, range)) / repeats)
			: InstrumentFunctionArpeggio::noteIndexForStep(step, direction, range, repeats, cycle);
		pattern.keys.push_back(index / chordSize * 12 + chord[index % chordSize]);
	}

	pattern.gate = m_a->m_arpGateModel.value() / 100.f;
	pattern.playChance = 1.f - m_a->m_arpSkipModel.value() / 100.f;
	pattern.enabled = m_a->m_arpEnabledModel.value();

	QStringList info;
	if (m_a->m_arpSkipModel.value() > 0) { info << tr("SKIP %1%").arg(m_a->m_arpSkipModel.value()); }
	if (m_a->m_arpMissModel.value() > 0) { info << tr("MISS %1%").arg(m_a->m_arpMissModel.value()); }
	pattern.info = info.join(" · ");
	pattern.time = tr("%1 ms / step").arg(qRound(m_a->m_arpTimeModel.value()));

	m_preview->setPattern(std::move(pattern));
}




} // namespace lmms::gui
