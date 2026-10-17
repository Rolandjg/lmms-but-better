/*
 * AudioFileProcessor.cpp - instrument for using audio files
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


#include "AudioFileProcessorView.h"

#include "AudioFileProcessor.h"
#include "AudioFileProcessorWaveView.h"

#include <QDragEnterEvent>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <functional>

#include "Clipboard.h"
#include "ComboBox.h"
#include "DataFile.h"
#include "Engine.h"
#include "FileDialog.h"
#include "FontHelper.h"
#include "InstrumentTrack.h"
#include "Knob.h"
#include "LcdSpinBox.h"
#include "ModernWidgets.h"
#include "Song.h"
#include "StringPairDrag.h"
#include "Track.h"


namespace lmms
{

namespace gui
{

namespace
{

constexpr int Margin = 8;
constexpr int HeaderHeight = 22;
constexpr int ToolbarTop = 36;
constexpr int WaveTop = 60;
constexpr int WaveHeight = 168;
constexpr int ColumnsTop = WaveTop + WaveHeight + 8;

//! Flat push button in the style of ModernToggle's off state, with text or a painted glyph
class FlatButton : public QPushButton
{
public:
	using Glyph = std::function<void(QPainter&, const QRectF&, const QColor&)>;

	FlatButton(const QString& text, QWidget* parent, Glyph glyph = {}) :
		QPushButton(text, parent),
		m_glyph(std::move(glyph))
	{
		setCursor(Qt::PointingHandCursor);
		setFont(adjustedToPixelSize(font(), SMALL_FONT_SIZE));
		setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	}

	QSize sizeHint() const override
	{
		if (m_glyph) { return QSize(HeaderHeight, HeaderHeight); }
		return QSize(QFontMetrics(font()).horizontalAdvance(text()) + 16, 18);
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
		p.setPen(QPen(modern::sectionBorder(), 1));
		p.setBrush(isDown() ? modern::activeButtonColor()
			: (underMouse() && isEnabled() ? modern::buttonColor().lighter(120) : modern::buttonColor()));
		p.drawRoundedRect(r, 2, 2);
		const QColor color = !isEnabled() ? modern::dimTextColor() : (isDown() ? modern::accentColor() : modern::textColor());
		if (m_glyph) { m_glyph(p, r.adjusted(5, 5, -5, -5), color); }
		else
		{
			p.setPen(color);
			p.drawText(rect(), Qt::AlignCenter, text());
		}
	}

private:
	Glyph m_glyph;
};

void paintFolder(QPainter& p, const QRectF& r, const QColor& color)
{
	QPainterPath path;
	path.moveTo(r.left(), r.top() + 1);
	path.lineTo(r.left() + r.width() * 0.4, r.top() + 1);
	path.lineTo(r.left() + r.width() * 0.5, r.top() + 3);
	path.lineTo(r.right(), r.top() + 3);
	path.lineTo(r.right(), r.bottom() - 1);
	path.lineTo(r.left(), r.bottom() - 1);
	path.closeSubpath();
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	p.drawPath(path);
}

void paintArrow(QPainter& p, const QRectF& r, const QColor& color, bool right)
{
	const QPointF c = r.center();
	QPolygonF arrow;
	const double w = 3.5, h = 5.0;
	arrow << QPointF(c.x() + (right ? -w : w), c.y() - h) << QPointF(c.x() + (right ? w : -w), c.y())
		<< QPointF(c.x() + (right ? -w : w), c.y() + h);
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	p.drawPolygon(arrow);
}

//! An unboxed group of controls: a small caps title and a hairline to its left
class Column : public QWidget
{
public:
	Column(const QString& title, QWidget* parent, bool divider = true) :
		QWidget(parent),
		m_title(title),
		m_divider(divider),
		m_grid(new QGridLayout(this))
	{
		m_grid->setContentsMargins(divider ? 10 : 2, 17, 4, 0);
		m_grid->setHorizontalSpacing(6);
		m_grid->setVerticalSpacing(5);
		m_grid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	}

	QGridLayout* grid() const { return m_grid; }
	void setTitle(const QString& title) { m_title = title; update(); }

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter p(this);
		auto f = adjustedToPixelSize(font(), SMALL_FONT_SIZE);
		f.setBold(true);
		f.setLetterSpacing(QFont::AbsoluteSpacing, 0.6);
		p.setFont(f);
		p.setPen(modern::dimTextColor());
		p.drawText(QPointF(m_grid->contentsMargins().left(), QFontMetrics(f).capHeight() + 2), m_title);
		if (m_divider)
		{
			p.setPen(QColor(255, 255, 255, 18));
			p.drawLine(QPointF(0.5, 0), QPointF(0.5, height()));
		}
	}

private:
	QString m_title;
	bool m_divider;
	QGridLayout* m_grid;
};

Knob* makeKnob(QWidget* parent, const QString& label, const QString& hint, const QString& unit, const QString& tip)
{
	auto knob = new Knob(KnobType::Modern, label, parent);
	knob->setHintText(hint, unit);
	knob->setToolTip(tip);
	return knob;
}

ModernToggle* makeToggle(const QString& text, QWidget* parent, const QString& tip)
{
	auto toggle = new ModernToggle(text, parent);
	toggle->setToolTip(tip);
	return toggle;
}

} // namespace


AudioFileProcessorView::AudioFileProcessorView(Instrument* instrument, QWidget* parent) :
	InstrumentView(instrument, parent)
{
	setFixedSize(sizeHint());
	modern::applyWindowStyle(this);
	setFont(adjustedToPixelSize(font(), SMALL_FONT_SIZE));
	auto afp = castModel<AudioFileProcessor>();

	// Header: open, previous / next file in the folder, the file name, and interpolation
	auto openButton = new FlatButton(QString(), this, paintFolder);
	openButton->setGeometry(Margin, Margin, HeaderHeight, HeaderHeight);
	openButton->setToolTip(tr("Open sample"));
	connect(openButton, &QPushButton::clicked, this, &AudioFileProcessorView::openAudioFile);

	auto previousButton = new FlatButton(QString(), this, [](QPainter& p, const QRectF& r, const QColor& c) { paintArrow(p, r, c, false); });
	previousButton->setGeometry(Margin + HeaderHeight + 4, Margin, 18, HeaderHeight);
	previousButton->setToolTip(tr("Previous sample in this folder"));
	connect(previousButton, &QPushButton::clicked, this, [afp] { afp->stepSampleFile(-1); });

	auto nextButton = new FlatButton(QString(), this, [](QPainter& p, const QRectF& r, const QColor& c) { paintArrow(p, r, c, true); });
	nextButton->setGeometry(Margin + HeaderHeight + 4 + 18 + 1, Margin, 18, HeaderHeight);
	nextButton->setToolTip(tr("Next sample in this folder"));
	connect(nextButton, &QPushButton::clicked, this, [afp] { afp->stepSampleFile(1); });

	m_interpBox = new ComboBox(this);
	m_interpBox->setGeometry(width() - Margin - 78, Margin, 78, HeaderHeight);
	m_interpBox->setToolTip(tr("Interpolation used when the sample is played at another pitch"));

	// Toolbar: playback mode and switches
	m_modeSelector = new ModernSegmented(this);
	m_modeSelector->setLabels({tr("CLASSIC"), tr("SLICE")});
	m_modeSelector->setToolTips({tr("Play the sample across the keyboard"),
		tr("Cut the sample into slices, one per key from the base note up")});
	m_modeSelector->setGeometry(Margin, ToolbarTop, 128, 18);

	m_reverseButton = makeToggle(tr("REVERSE"), this, tr("Play the sample backwards"));
	m_stutterButton = makeToggle(tr("STUTTER"), this, tr("Continue sample playback across notes"));
	m_oneShotButton = makeToggle(tr("ONE-SHOT"), this, tr("Play to the end of the sample (or slice) however short the note is"));
	m_chokeButton = makeToggle(tr("CHOKE"), this, tr("A new note cuts off the one still playing, like a closed hi-hat or an MPC pad"));
	m_snapButton = makeToggle(tr("SNAP"), this, tr("Snap start, end, loop and slice points to zero crossings so they don't click\n(hold Shift while dragging to place them freely)"));
	int x = width() - Margin;
	for (auto toggle : {m_snapButton, m_chokeButton, m_oneShotButton, m_stutterButton, m_reverseButton})
	{
		const int w = toggle->sizeHint().width();
		x -= w;
		toggle->setGeometry(x, ToolbarTop, w, 18);
		x -= 4;
	}

	// The waveform editor
	m_waveView = new AudioFileProcessorWaveView(this, afp);
	m_waveView->setGeometry(Margin, WaveTop, width() - 2 * Margin, WaveHeight);
	m_waveView->setToolTip(QString());
	connect(m_waveView, &AudioFileProcessorWaveView::openFileRequested, this, &AudioFileProcessorView::openAudioFile);

	// Control columns
	auto columns = new QWidget(this);
	columns->setGeometry(Margin, ColumnsTop, width() - 2 * Margin, height() - ColumnsTop - 4);
	auto row = new QHBoxLayout(columns);
	row->setContentsMargins(0, 0, 0, 0);
	row->setSpacing(0);

	// Gain
	auto gain = new Column(tr("GAIN"), columns, false);
	m_ampKnob = new Knob(KnobType::Modern, tr("AMP"), gain);
	m_ampKnob->setHintText(tr("Amplify:"), "%");
	m_ampKnob->setToolTip(tr("Volume of the sample (100% is unchanged)"));
	auto normalizeButton = new FlatButton(tr("NORM"), gain);
	normalizeButton->setToolTip(tr("Set the volume so the loudest point between start and end reaches 0 dB"));
	connect(normalizeButton, &QPushButton::clicked, this, [afp] { afp->normalize(); });
	gain->grid()->addWidget(m_ampKnob, 0, 0, Qt::AlignHCenter);
	gain->grid()->addWidget(normalizeButton, 1, 0);
	row->addWidget(gain);

	// Region (classic) or slicing (slice mode)
	auto region = new Column(tr("REGION"), columns);
	m_regionStack = new QStackedWidget(region);
	region->grid()->addWidget(m_regionStack, 0, 0);

	auto regionPage = new QWidget(m_regionStack);
	auto regionLayout = new QHBoxLayout(regionPage);
	regionLayout->setContentsMargins(0, 0, 0, 0);
	regionLayout->setSpacing(6);
	regionLayout->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	m_startKnob = makeKnob(regionPage, tr("START"), tr("Start point:"), "", tr("Where playback starts (drag the S line in the waveform)"));
	m_loopKnob = makeKnob(regionPage, tr("LOOP"), tr("Loopback point:"), "", tr("Where a loop jumps back to (drag the L line in the waveform)"));
	m_endKnob = makeKnob(regionPage, tr("END"), tr("End point:"), "", tr("Where playback ends (drag the E line in the waveform)"));
	m_fadeInKnob = makeKnob(regionPage, tr("FADE IN"), tr("Fade in:"), " ms",
		tr("Fade in from the start point (or drag the square at the top of the waveform)"));
	m_fadeOutKnob = makeKnob(regionPage, tr("FADE OUT"), tr("Fade out:"), " ms",
		tr("Fade out towards the end point when not looping (or drag the square at the top of the waveform)"));
	for (auto knob : {m_startKnob, m_loopKnob, m_endKnob, m_fadeInKnob, m_fadeOutKnob}) { regionLayout->addWidget(knob); }
	m_regionStack->addWidget(regionPage);

	auto slicePage = new QWidget(m_regionStack);
	auto sliceLayout = new QGridLayout(slicePage);
	sliceLayout->setContentsMargins(0, 0, 0, 0);
	sliceLayout->setHorizontalSpacing(6);
	sliceLayout->setVerticalSpacing(5);
	sliceLayout->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	m_sliceMethod = new ModernSegmented(slicePage);
	m_sliceMethod->setLabels({tr("AUTO"), tr("GRID"), tr("MANUAL")});
	m_sliceMethod->setToolTips({tr("Slice at the transients (hits) of the sample"),
		tr("Slice on a beat grid, using the sample tempo"),
		tr("Place slices by hand: double-click the waveform to add one, double-click a marker to remove it")});
	m_sliceMethod->setFixedWidth(140);
	sliceLayout->addWidget(m_sliceMethod, 0, 0);

	m_sliceParamStack = new QStackedWidget(slicePage);
	m_sensitivityKnob = makeKnob(m_sliceParamStack, tr("SENS"), tr("Sensitivity:"), "%", tr("Higher finds more, quieter hits"));
	auto sensPage = new QWidget(m_sliceParamStack);
	auto sensLayout = new QHBoxLayout(sensPage);
	sensLayout->setContentsMargins(0, 0, 0, 0);
	sensLayout->addWidget(m_sensitivityKnob, 0, Qt::AlignLeft | Qt::AlignTop);
	m_sliceParamStack->addWidget(sensPage);
	auto gridPage = new QWidget(m_sliceParamStack);
	auto gridLayout = new QVBoxLayout(gridPage);
	gridLayout->setContentsMargins(0, 0, 0, 0);
	m_divisionSelector = new ModernSegmented(gridPage);
	m_divisionSelector->setLabels({"1/16", "1/8", "1/4", "1/2", tr("BAR")});
	m_divisionSelector->setToolTips({tr("A slice every 16th note"), tr("A slice every 8th note"),
		tr("A slice every beat"), tr("A slice every half bar"), tr("A slice every bar")});
	gridLayout->addWidget(m_divisionSelector, 0, Qt::AlignTop);
	m_sliceParamStack->addWidget(gridPage);
	auto manualHint = new QLabel(tr("Double-click to add\nor remove a slice"), m_sliceParamStack);
	manualHint->setStyleSheet(QString("color: %1;").arg(modern::dimTextColor().name()));
	manualHint->setFont(adjustedToPixelSize(font(), SMALL_FONT_SIZE));
	manualHint->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	m_sliceParamStack->addWidget(manualHint);
	m_sliceParamStack->setFixedSize(140, 42);
	sliceLayout->addWidget(m_sliceParamStack, 1, 0);

	m_sliceCount = new QLabel(slicePage);
	m_sliceCount->setStyleSheet(QString("color: %1;").arg(modern::textColor().name()));
	m_sliceCount->setFixedHeight(18);
	m_sliceCount->setFont(adjustedToPixelSize(font(), SMALL_FONT_SIZE));
	auto copyButton = new FlatButton(tr("COPY MIDI"), slicePage);
	copyButton->setToolTip(tr("Copy one note per slice, in the original rhythm, to paste into the piano roll"));
	connect(copyButton, &QPushButton::clicked, this, [this, afp]
	{
		afp->copySlicesAsNotes();
		m_status = tr("Notes copied: paste them in the piano roll");
		update();
		QTimer::singleShot(3000, this, [this] { m_status.clear(); update(); });
	});
	sliceLayout->addWidget(m_sliceCount, 0, 1);
	sliceLayout->addWidget(copyButton, 1, 1, Qt::AlignTop);
	m_regionStack->addWidget(slicePage);
	row->addWidget(region, 1);

	// Loop
	auto loop = new Column(tr("LOOP"), columns);
	m_loopSelector = new ModernSegmented(loop);
	m_loopSelector->setLabels({tr("OFF"), tr("ON"), tr("PING")});
	m_loopSelector->setToolTips({tr("Play once"), tr("Loop from the loop point to the end"),
		tr("Loop back and forth between the loop point and the end")});
	m_loopSelector->setFixedWidth(96);
	m_crossfadeKnob = makeKnob(loop, tr("X-FADE"), tr("Loop crossfade:"), " ms",
		tr("Crossfade the loop end into the loop point to remove clicks"));
	loop->grid()->addWidget(m_loopSelector, 0, 0);
	loop->grid()->addWidget(m_crossfadeKnob, 1, 0, Qt::AlignLeft);
	row->addWidget(loop);

	// Pitch
	auto pitch = new Column(tr("PITCH"), columns);
	m_transposeKnob = makeKnob(pitch, tr("SEMI"), tr("Transpose:"), tr(" semitones"), tr("Transpose in semitones"));
	m_tuneKnob = makeKnob(pitch, tr("FINE"), tr("Fine tune:"), tr(" cents"), tr("Fine tune in cents"));
	auto detectButton = new FlatButton(tr("DETECT KEY"), pitch);
	detectButton->setToolTip(tr("Find the pitch of the sample and set the base note, so the keyboard plays it in tune"));
	connect(detectButton, &QPushButton::clicked, this, [this, afp]
	{
		static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
		if (const float frequency = afp->detectRootKey(); frequency > 0.f)
		{
			const int key = afp->instrumentTrack()->baseNoteModel()->value();
			m_status = tr("Detected %1%2 (%3 Hz), set as base note").arg(names[key % 12]).arg(key / 12 - 1)
				.arg(frequency, 0, 'f', 1);
		}
		else { m_status = tr("No clear pitch found"); }
		update();
		QTimer::singleShot(4000, this, [this] { m_status.clear(); update(); });
	});
	m_detectButton = detectButton;
	pitch->grid()->addWidget(m_transposeKnob, 0, 0, Qt::AlignHCenter);
	pitch->grid()->addWidget(m_tuneKnob, 0, 1, Qt::AlignHCenter);
	pitch->grid()->addWidget(detectButton, 1, 0, 1, 2);
	row->addWidget(pitch);

	// Warp
	auto warp = new Column(tr("WARP"), columns);
	m_warpButton = makeToggle(tr("WARP"), warp, tr("Time-stretch the sample to the song tempo without changing its pitch"));
	m_warpButton->setFixedWidth(52);
	m_tempoSpinBox = new LcdSpinBox(3, warp, tr("Sample tempo"));
	m_tempoSpinBox->setLabel(tr("BPM"));
	m_tempoSpinBox->setToolTip(tr("Original tempo of the sample (detected when it is loaded). Also sets the slice grid."));
	warp->grid()->addWidget(m_warpButton, 0, 0);
	warp->grid()->addWidget(m_tempoSpinBox, 1, 0, Qt::AlignLeft);
	row->addWidget(warp);

	m_columns = {gain, region, loop, pitch, warp};

	setAcceptDrops(true);
}




void AudioFileProcessorView::dragEnterEvent(QDragEnterEvent* dee)
{
	// For mimeType() and MimeType enum class
	using namespace Clipboard;

	if (dee->mimeData()->hasFormat(mimeType(MimeType::StringPair)))
	{
		QString txt = dee->mimeData()->data(
						mimeType(MimeType::StringPair));
		if (txt.section(':', 0, 0) == QString("clip_%1").arg(
							static_cast<int>(Track::Type::Sample)))
		{
			dee->acceptProposedAction();
		}
		else if (txt.section(':', 0, 0) == "samplefile")
		{
			dee->acceptProposedAction();
		}
		else
		{
			dee->ignore();
		}
	}
	else if (dee->mimeData()->hasUrls() && dee->mimeData()->urls().value(0).isLocalFile())
	{
		// Files dragged in from the desktop or a file manager
		dee->acceptProposedAction();
	}
	else
	{
		dee->ignore();
	}
}




void AudioFileProcessorView::dropEvent(QDropEvent* de)
{
	auto afp = castModel<AudioFileProcessor>();
	if (de->mimeData()->hasUrls() && !de->mimeData()->hasFormat(Clipboard::mimeType(Clipboard::MimeType::StringPair)))
	{
		afp->setAudioFile(de->mimeData()->urls().value(0).toLocalFile());
		Engine::getSong()->setModified();
		de->accept();
		return;
	}

	const auto type = StringPairDrag::decodeKey(de);
	const auto value = StringPairDrag::decodeValue(de);

	if (type == "samplefile") { afp->setAudioFile(value); }
	else if (type == QString("clip_%1").arg(static_cast<int>(Track::Type::Sample)))
	{
		DataFile dataFile(value.toUtf8());
		afp->setAudioFile(dataFile.content().firstChild().toElement().attribute("src"));
	}
	else
	{
		de->ignore();
		return;
	}

	Engine::getSong()->setModified();
	de->accept();
}




QRect AudioFileProcessorView::fileNameRect() const
{
	const int left = Margin + HeaderHeight + 4 + 2 * 18 + 1 + 8;
	return QRect(left, Margin, m_interpBox->x() - 8 - left, HeaderHeight);
}




void AudioFileProcessorView::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	auto afp = castModel<AudioFileProcessor>();
	const auto& sample = afp->sample();
	const QRect nameRect = fileNameRect();

	// File name on the first line, details on the second
	auto nameFont = adjustedToPixelSize(font(), DEFAULT_FONT_SIZE);
	nameFont.setBold(true);
	const auto file = sample.sampleFile();
	const auto name = file.isEmpty() ? tr("No sample loaded") : QFileInfo(file).fileName();
	p.setFont(nameFont);
	p.setPen(file.isEmpty() ? modern::dimTextColor() : modern::textColor());
	const auto nameMetrics = QFontMetrics(nameFont);
	p.drawText(QPointF(nameRect.left(), nameRect.top() + nameMetrics.capHeight() + 1),
		nameMetrics.elidedText(name, Qt::ElideMiddle, nameRect.width()));

	QString details = m_status;
	if (details.isEmpty() && sample.sampleSize() > 1)
	{
		const double seconds = static_cast<double>(sample.sampleSize()) / std::max(1, sample.sampleRate());
		details = tr("%1 kHz · %2 s").arg(sample.sampleRate() / 1000.0, 0, 'f', 1).arg(seconds, 0, 'f', seconds < 10 ? 2 : 1);
		if (afp->warpModel().value())
		{
			details += tr(" · warped %1 → %2 BPM").arg(afp->sampleTempoModel().value()).arg(Engine::getSong()->getTempo());
		}
		if (afp->sliceMode())
		{
			const auto slices = afp->sliceBoundaries().size() - 1;
			const int baseNote = afp->instrumentTrack()->baseNote();
			static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
			const auto keyName = [](int key) { return QString("%1%2").arg(names[((key % 12) + 12) % 12]).arg(key / 12 - 1); };
			details += tr(" · keys %1–%2").arg(keyName(baseNote)).arg(keyName(baseNote + static_cast<int>(slices) - 1));
		}
	}
	else if (details.isEmpty()) { details = tr("Drop an audio file anywhere on this window"); }
	const auto smallFont = adjustedToPixelSize(font(), SMALL_FONT_SIZE);
	p.setFont(smallFont);
	p.setPen(m_status.isEmpty() ? modern::dimTextColor() : modern::accentColor());
	p.drawText(QPointF(nameRect.left(), nameRect.bottom()),
		QFontMetrics(smallFont).elidedText(details, Qt::ElideRight, nameRect.width()));
}




void AudioFileProcessorView::mousePressEvent(QMouseEvent* me)
{
	if (me->button() == Qt::LeftButton && fileNameRect().contains(me->pos())) { openAudioFile(); }
	else { InstrumentView::mousePressEvent(me); }
}




void AudioFileProcessorView::sampleUpdated()
{
	m_waveView->sampleUpdated();
	updateModeWidgets();
}




void AudioFileProcessorView::openAudioFile()
{
	QString af = FileDialog::openAudioFile();
	if (af.isEmpty()) { return; }

	castModel<AudioFileProcessor>()->setAudioFile(af);
	Engine::getSong()->setModified();
}




void AudioFileProcessorView::updateModeWidgets()
{
	auto afp = castModel<AudioFileProcessor>();
	const bool slicing = afp->sliceMode();
	m_regionStack->setCurrentIndex(slicing ? 1 : 0);
	static_cast<Column*>(m_columns[1])->setTitle(slicing ? tr("SLICES") : tr("REGION"));
	m_sliceParamStack->setCurrentIndex(std::clamp(afp->sliceMethodModel().value(), 0, 2));
	m_stutterButton->setEnabled(!slicing);
	m_crossfadeKnob->setEnabled(!slicing);
	m_detectButton->setEnabled(!slicing);
	const auto slices = static_cast<int>(afp->sliceBoundaries().size()) - 1;
	m_sliceCount->setText(slices == 1 ? tr("1 slice") : tr("%1 slices").arg(slices));
	update();
}




void AudioFileProcessorView::modelChanged()
{
	auto a = castModel<AudioFileProcessor>();
	connect(a, &AudioFileProcessor::sampleUpdated, this, &AudioFileProcessorView::sampleUpdated);
	connect(a, &AudioFileProcessor::slicesChanged, this, &AudioFileProcessorView::updateModeWidgets);
	connect(a, &AudioFileProcessor::dataChanged, this, &AudioFileProcessorView::updateModeWidgets);
	m_ampKnob->setModel(&a->ampModel());
	m_startKnob->setModel(&a->startPointModel());
	m_endKnob->setModel(&a->endPointModel());
	m_loopKnob->setModel(&a->loopPointModel());
	m_reverseButton->setModel(&a->reverseModel());
	m_loopSelector->setModel(&a->loopModel());
	m_stutterButton->setModel(&a->stutterModel());
	m_interpBox->setModel(&a->interpolationModel());
	m_warpButton->setModel(&a->warpModel());
	m_tempoSpinBox->setModel(&a->sampleTempoModel());
	m_crossfadeKnob->setModel(&a->crossfadeModel());
	m_modeSelector->setModel(&a->modeModel());
	m_sliceMethod->setModel(&a->sliceMethodModel());
	m_sensitivityKnob->setModel(&a->sliceSensitivityModel());
	m_divisionSelector->setModel(&a->sliceDivisionModel());
	m_oneShotButton->setModel(&a->oneShotModel());
	m_chokeButton->setModel(&a->chokeModel());
	m_snapButton->setModel(&a->snapModel());
	m_transposeKnob->setModel(&a->transposeModel());
	m_tuneKnob->setModel(&a->tuneModel());
	m_fadeInKnob->setModel(&a->fadeInModel());
	m_fadeOutKnob->setModel(&a->fadeOutModel());
	for (Model* model : std::initializer_list<Model*>{&a->modeModel(), &a->sliceMethodModel()})
	{
		connect(model, &Model::dataChanged, this, &AudioFileProcessorView::updateModeWidgets);
	}
	for (Model* model : std::initializer_list<Model*>{&a->warpModel(), &a->sampleTempoModel(), a->instrumentTrack()->baseNoteModel()})
	{
		connect(model, &Model::dataChanged, this, qOverload<>(&QWidget::update));
	}
	connect(Engine::getSong(), &Song::tempoChanged, this, qOverload<>(&QWidget::update));
	updateModeWidgets();
	sampleUpdated();
}

} // namespace gui

} // namespace lmms
