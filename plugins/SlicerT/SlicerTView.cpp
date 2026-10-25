/*
 * SlicerTView.cpp - controls the UI for slicerT
 *
 * Copyright (c) 2023 Daniel Kauss Serna <daniel.kauss.serna@gmail.com>
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

#include "SlicerTView.h"

#include <QDropEvent>
#include <QPainter>
#include <QPushButton>

#include "Clipboard.h"
#include "ComboBox.h"
#include "DataFile.h"
#include "FileDialog.h"
#include "InstrumentView.h"
#include "Knob.h"
#include "LcdSpinBox.h"
#include "ModernWidgets.h"
#include "PixmapButton.h"
#include "SlicerT.h"
#include "SlicerTWaveform.h"
#include "StringPairDrag.h"
#include "Track.h"
#include "embed.h"

namespace lmms {

namespace gui {

SlicerTView::SlicerTView(SlicerT* instrument, QWidget* parent)
	: InstrumentView(instrument, parent)
	, m_slicerTParent(instrument)
	, m_fullLogo(PLUGIN_NAME::getIconPixmap("full_logo"))
	, m_background(PLUGIN_NAME::getIconPixmap("toolbox"))
{
	// window settings
	setAcceptDrops(true);
	setAutoFillBackground(true);

	setMaximumSize(QSize(10000, 10000));
	setMinimumSize(QSize(560, 400));

	m_wf = new SlicerTWaveform(248, 128, instrument, this);
	m_wf->move(0, s_topBarHeight);

	m_snapSetting = new ComboBox(this, tr("Slice snap"));
	m_snapSetting->setGeometry(0, 0, s_snapBoxWidth, ComboBox::DEFAULT_HEIGHT);
	m_snapSetting->setToolTip(tr("Snap detected slices to the beat (the grid size in Grid mode)"));
	m_snapSetting->setModel(&m_slicerTParent->m_sliceSnap);

	m_sliceModeBox = new ComboBox(this, tr("Slice mode"));
	m_sliceModeBox->setGeometry(0, 0, s_modeBoxWidth, ComboBox::DEFAULT_HEIGHT);
	m_sliceModeBox->setToolTip(tr("How the sample is sliced:\n"
		"Transient - at detected hits, sensitivity set by Threshold\n"
		"Grid - on the beat grid set by Snap and BPM\n"
		"Equal - into equal parts, count set by Slices\n"
		"When the editor is zoomed in, only the part shown is sliced again"));
	m_sliceModeBox->setModel(&m_slicerTParent->m_sliceMode);

	m_sliceCountBox = new LcdSpinBox(3, "19purple", this);
	m_sliceCountBox->setToolTip(tr("Number of slices in Equal mode"));
	m_sliceCountBox->setModel(&m_slicerTParent->m_sliceCount);

	m_chokeToggle = new ModernToggle(tr("CHOKE"), this);
	m_chokeToggle->setToolTip(tr("A new note cuts off the one still playing, like an MPC pad"));
	m_chokeToggle->setModel(&m_slicerTParent->m_choke);

	m_oneShotToggle = new ModernToggle(tr("ONE-SHOT"), this);
	m_oneShotToggle->setToolTip(tr("Play the whole slice however short the note is"));
	m_oneShotToggle->setModel(&m_slicerTParent->m_oneShot);

	m_syncToggle = new PixmapButton(this, tr("Sync sample"));
	m_syncToggle->setActiveGraphic(PLUGIN_NAME::getIconPixmap("sync_active"));
	m_syncToggle->setInactiveGraphic(PLUGIN_NAME::getIconPixmap("sync_inactive"));
	m_syncToggle->setCheckable(true);
	m_syncToggle->setToolTip(tr("Enable BPM sync"));
	m_syncToggle->setModel(&m_slicerTParent->m_enableSync);

	m_clearButton = new PixmapButton(this, tr("Clear all slices"));
	m_clearButton->setActiveGraphic(PLUGIN_NAME::getIconPixmap("clear_slices_active"));
	m_clearButton->setInactiveGraphic(PLUGIN_NAME::getIconPixmap("clear_slices_inactive"));
	m_clearButton->setToolTip(tr("Clear all slices"));
	connect(m_clearButton, &PixmapButton::clicked, this, &SlicerTView::clearSlices);

	m_bpmBox = new LcdSpinBox(3, "19purple", this);
	m_bpmBox->setToolTip(tr("Original sample BPM"));
	m_bpmBox->setModel(&m_slicerTParent->m_originalBPM);

	m_noteThresholdKnob = createStyledKnob();
	m_noteThresholdKnob->setToolTip(tr("Threshold used for slicing"));
	m_noteThresholdKnob->setModel(&m_slicerTParent->m_noteThreshold);

	m_fadeOutKnob = createStyledKnob();
	m_fadeOutKnob->setToolTip(tr("Fade Out per note in milliseconds"));
	m_fadeOutKnob->setModel(&m_slicerTParent->m_fadeOutFrames);

	m_midiExportButton = new QPushButton(this);
	m_midiExportButton->setFixedSize(s_iconButtonSize, s_iconButtonSize);
	m_midiExportButton->setIcon(PLUGIN_NAME::getIconPixmap("copy_midi"));
	m_midiExportButton->setToolTip(tr("Copy midi pattern to clipboard"));
	connect(m_midiExportButton, &PixmapButton::clicked, this, &SlicerTView::exportMidi);

	m_folderButton = new PixmapButton(this);
	m_folderButton->setActiveGraphic(PLUGIN_NAME::getIconPixmap("folder_icon"));
	m_folderButton->setInactiveGraphic(PLUGIN_NAME::getIconPixmap("folder_icon"));
	m_folderButton->setToolTip(tr("Open sample selector"));
	connect(m_folderButton, &PixmapButton::clicked, this, &SlicerTView::openFiles);

	// A push button: it lights up while held, then slices again
	m_sliceButton = new ModernToggle(tr("SLICE"), this);
	m_sliceButton->setCheckable(false);
	m_sliceButton->setToolTip(tr("Slice again with the current settings.\n"
		"Zoom in with the top bar of the editor to only slice that part of the sample."));
	connect(m_sliceButton, &ModernToggle::clicked, m_slicerTParent, &SlicerT::updateSlices);

	update();
}

Knob* SlicerTView::createStyledKnob()
{
	Knob* newKnob = new Knob(KnobType::Styled, this);
	newKnob->setFixedSize(50, 40);
	newKnob->setCenterPointX(24.0);
	newKnob->setCenterPointY(15.0);
	return newKnob;
}

// Clear all notes
void SlicerTView::clearSlices()
{
	m_slicerTParent->clearSlices();
}

// copied from piano roll
void SlicerTView::exportMidi()
{
	using namespace Clipboard;
	if (m_slicerTParent->m_originalSample.sampleSize() <= 1) { return; }

	DataFile dataFile(DataFile::Type::ClipboardData);
	QDomElement noteList = dataFile.createElement("note-list");
	dataFile.content().appendChild(noteList);

	auto notes = m_slicerTParent->getMidi();
	if (notes.empty()) { return; }

	TimePos startPos(notes.front().pos().getBar(), 0);
	for (Note& note : notes)
	{
		note.setPos(note.pos(startPos));
		note.saveState(dataFile, noteList);
	}

	copyString(dataFile.toString(), MimeType::Default);
}

void SlicerTView::openFiles()
{
	const auto audioFile = FileDialog::openAudioFile();
	if (audioFile.isEmpty()) { return; }
	m_slicerTParent->updateFile(audioFile);
}

// all the drag stuff is copied from AudioFileProcessor
void SlicerTView::dragEnterEvent(QDragEnterEvent* dee)
{
	// For mimeType() and MimeType enum class
	using namespace Clipboard;

	if (dee->mimeData()->hasFormat(mimeType(MimeType::StringPair)))
	{
		QString txt = dee->mimeData()->data(mimeType(MimeType::StringPair));
		if (txt.section(':', 0, 0) == QString("clip_%1").arg(static_cast<int>(Track::Type::Sample)))
		{
			dee->acceptProposedAction();
		}
		else if (txt.section(':', 0, 0) == "samplefile") { dee->acceptProposedAction(); }
		else { dee->ignore(); }
	}
	else { dee->ignore(); }
}

void SlicerTView::dropEvent(QDropEvent* de)
{
	QString type = StringPairDrag::decodeKey(de);
	QString value = StringPairDrag::decodeValue(de);
	if (type == "samplefile")
	{
		// set m_wf wave file
		m_slicerTParent->updateFile(value);
		return;
	}
	else if (type == QString("clip_%1").arg(static_cast<int>(Track::Type::Sample)))
	{
		DataFile dataFile(value.toUtf8());
		m_slicerTParent->updateFile(dataFile.content().firstChild().toElement().attribute("src"));
		de->accept();
		return;
	}

	de->ignore();
}

void SlicerTView::paintEvent(QPaintEvent* pe)
{
	QPainter brush(this);
	brush.setFont(QFont(brush.font().family(), 7, -1, false));

	int boxTopY = height() - s_bottomBoxHeight;

	// --- backgrounds and limiters
	brush.drawPixmap(QRect(0, boxTopY, s_leftBoxWidth, s_bottomBoxHeight), m_background); // left
	brush.fillRect(
		QRect(s_leftBoxWidth, boxTopY, width() - s_leftBoxWidth, s_bottomBoxHeight), QColor(23, 26, 31)); // right
	brush.fillRect(QRect(0, 0, width(), s_topBarHeight), QColor(20, 23, 27));							  // top

	// top bar dividers
	brush.setPen(QColor(56, 58, 60));
	brush.drawLine(0, s_topBarHeight - 1, width(), s_topBarHeight - 1);
	brush.drawLine(0, 0, width(), 0);

	// sample name divider
	brush.setPen(QColor(56, 58, 60));
	brush.drawLine(0, boxTopY, width(), boxTopY);

	// boxes divider
	brush.setPen(QColor(56, 24, 94));
	brush.drawLine(s_leftBoxWidth, boxTopY, s_leftBoxWidth, height());

	// --- top bar
	brush.drawPixmap(
		QRect(10, (s_topBarHeight - m_fullLogo.height()) / 2, m_fullLogo.width(), m_fullLogo.height()), m_fullLogo);

	int y1_text = m_y1 + 27;

	// --- left box
	// labels are centered below their control
	const auto drawLabel = [&](QWidget* control, const QString& text) {
		const int center = control->x() + control->width() / 2;
		brush.drawText(center - s_textBoxWidth / 2, y1_text, s_textBoxWidth, s_textBoxHeight, Qt::AlignCenter, text);
	};
	brush.setPen(QColor(255, 255, 255));
	drawLabel(m_noteThresholdKnob, tr("Threshold"));
	drawLabel(m_fadeOutKnob, tr("Fade Out"));
	drawLabel(m_sliceModeBox, tr("Mode"));
	drawLabel(m_sliceCountBox, tr("Slices"));
	drawLabel(m_bpmBox, tr("BPM"));
	drawLabel(m_snapSetting, tr("Snap"));
	drawLabel(m_midiExportButton, tr("Midi"));

	int kor = 15; // knob outer radius
	int kir = 9;  // knob inner radius

	// draw knob backgrounds
	brush.setRenderHint(QPainter::Antialiasing);

	// draw outer radius 2 times to make smooth
	brush.setPen(QPen(QColor(159, 124, 223, 100), 4));
	brush.drawArc(QRect(s_x1 - kor, m_y1, kor * 2, kor * 2), -45 * 16, 270 * 16);
	brush.drawArc(QRect(s_x2 - kor, m_y1, kor * 2, kor * 2), -45 * 16, 270 * 16);

	brush.setPen(QPen(QColor(159, 124, 223, 255), 2));
	brush.drawArc(QRect(s_x1 - kor, m_y1, kor * 2, kor * 2), -45 * 16, 270 * 16);
	brush.drawArc(QRect(s_x2 - kor, m_y1, kor * 2, kor * 2), -45 * 16, 270 * 16);

	// inner knob circle
	brush.setBrush(QColor(106, 90, 138));
	brush.setPen(QColor(0, 0, 0, 0));
	brush.drawEllipse(QPoint(s_x1, m_y1 + 15), kir, kir);
	brush.drawEllipse(QPoint(s_x2, m_y1 + 15), kir, kir);

	// current sample bar
	brush.fillRect(QRect(0, boxTopY - s_sampleBoxHeight, width(), s_sampleBoxHeight), QColor(5, 5, 5));

	brush.setPen(QColor(56, 58, 60));
	brush.drawLine(width() - 24, boxTopY - s_sampleBoxHeight, width() - 24, boxTopY);

	brush.setPen(QColor(255, 255, 255, 180));
	brush.setFont(QFont(brush.font().family(), 8, -1, false));
	QString sampleName = m_slicerTParent->getSampleName();
	if (sampleName == "") { sampleName = "No sample loaded"; }

	brush.drawText(5, boxTopY - s_sampleBoxHeight, width(), s_sampleBoxHeight, Qt::AlignLeft, sampleName);
}

void SlicerTView::resizeEvent(QResizeEvent* re)
{
	m_y1 = height() - s_bottomBoxOffset;

	// Left box
	m_noteThresholdKnob->move(s_x1 - 25, m_y1);
	m_fadeOutKnob->move(s_x2 - 25, m_y1);

	m_sliceModeBox->move(s_x3, m_y1 + 4);
	m_sliceCountBox->move(s_x4, m_y1 + 4);
	m_bpmBox->move(s_x5, m_y1 + 4);
	m_snapSetting->move(s_x6, m_y1 + 4);

	m_midiExportButton->move(s_x7, m_y1 + 1);

	// Top bar, right aligned
	int toggleX = width() - 10;
	for (auto toggle : {m_oneShotToggle, m_chokeToggle, m_sliceButton})
	{
		const int toggleWidth = toggle->sizeHint().width();
		toggleX -= toggleWidth;
		toggle->setGeometry(toggleX, (s_topBarHeight - 18) / 2, toggleWidth, 18);
		// The slice button is an action, kept apart from the playback toggles
		toggleX -= toggle == m_chokeToggle ? 16 : 6;
	}

	// Right box
	// For explanation on the choice of constants, look at #7850
	m_syncToggle->move((width() - 100), m_y1 - 7);
	m_clearButton->move((width() - 100), m_y1 + 17);

	m_folderButton->move(width() - 20, height() - s_bottomBoxHeight - s_sampleBoxHeight + 1);

	int waveFormHeight = height() - s_bottomBoxHeight - s_topBarHeight - s_sampleBoxHeight;

	m_wf->resize(width(), waveFormHeight);
}

} // namespace gui
} // namespace lmms
