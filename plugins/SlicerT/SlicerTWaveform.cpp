/*
 * SlicerTWaveform.cpp - slice editor for SlicerT
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

#include "SlicerTWaveform.h"

#include <QApplication>
#include <QBitmap>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include "DeprecationHelper.h"
#include "InstrumentTrack.h"
#include "MidiEvent.h"
#include "SampleThumbnail.h"
#include "SlicerT.h"
#include "SlicerTView.h"
#include "embed.h"

namespace lmms {

namespace gui {

namespace {

QString keyName(int key)
{
	static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
	if (key < 0 || key >= NumKeys) { return {}; }
	return QString("%1%2").arg(names[key % 12]).arg(key / 12 - 1);
}

} // namespace

// waveform colors
static QColor s_emptyColor = QColor(0, 0, 0, 0);
static QColor s_waveformColor = QColor(123, 49, 212); // color of outer waveform
static QColor s_waveformSeekerBgColor = QColor(0, 0, 0, 255);
static QColor s_waveformEditorBgColor = QColor(15, 15, 15, 255);
static QColor s_waveformMaskColor = QColor(151, 65, 255);	// update this if s_waveformColor changes
static QColor s_waveformInnerColor = QColor(183, 124, 255); // color of inner waveform

// now playing colors
static QColor s_playColor = QColor(255, 255, 255, 200);			// now playing line
static QColor s_playHighlightColor = QColor(255, 255, 255, 70); // now playing note marker

// slice markers
static QColor s_sliceColor = QColor(218, 193, 255);			 // color of slice marker
static QColor s_sliceShadowColor = QColor(136, 120, 158);	 // color of dark side of slice marker
static QColor s_sliceHighlightColor = QColor(255, 255, 255); // color of highlighted slice marker

// seeker rect colors
static QColor s_seekerColor = QColor(178, 115, 255);			   // outline of seeker
static QColor s_seekerHighlightColor = QColor(178, 115, 255, 100); // inside of seeker
static QColor s_seekerShadowColor = QColor(0, 0, 0, 120);		   // color used for darkening outside seeker

// decor colors
static QColor s_editorBounding = QColor(53, 22, 90); // color of the editor bounding box
static QColor s_gradientEnd = QColor(29, 16, 47);	 // end color of the seeker gradient

SlicerTWaveform::SlicerTWaveform(int totalWidth, int totalHeight, SlicerT* instrument, QWidget* parent)
	: QWidget(parent)
	, m_width(totalWidth)
	, m_height(totalHeight)
	, m_seekerHeight(40)
	, m_seekerWidth(totalWidth - s_seekerHorMargin * 2)
	, m_editorHeight(totalHeight - m_seekerHeight - s_middleMargin - s_seekerVerMargin)
	, m_editorWidth(totalWidth)
	, m_sliceArrow(PLUGIN_NAME::getIconPixmap("slice_indicator_arrow"))
	, m_seeker(QPixmap(m_seekerWidth, m_seekerHeight))
	, m_seekerWaveform(QPixmap(m_seekerWidth, m_seekerHeight))
	, m_editorWaveform(QPixmap(m_editorWidth, m_editorHeight - s_arrowHeight))
	, m_sliceEditor(QPixmap(totalWidth, m_editorHeight))
	, m_emptySampleIcon(embed::getIconPixmap("sample_track"))
	, m_slicerTParent(instrument)
{
	setMouseTracking(true);

	m_seekerWaveform.fill(s_waveformSeekerBgColor);
	m_editorWaveform.fill(s_waveformEditorBgColor);

	connect(instrument, &SlicerT::isPlaying, this, &SlicerTWaveform::isPlaying);
	connect(instrument, &SlicerT::dataChanged, this, &SlicerTWaveform::updateUI);

	m_emptySampleIcon = m_emptySampleIcon.createMaskFromColor(QColor(255, 255, 255), Qt::MaskMode::MaskOutColor);

	setToolTip(tr("Drag the edges of the top bar to zoom in. Slicing again only changes the part shown below.\n"
		"Double-click: add a slice\n"
		"Drag a slice: move it\n"
		"Right-click a slice: remove it\n"
		"Alt + click: play the slice under the cursor\n"
		"Middle-click: show the whole sample"));

	m_updateTimer.start();
	updateUI();
}

SlicerTWaveform::~SlicerTWaveform()
{
	stopAudition();
}

void SlicerTWaveform::resizeEvent(QResizeEvent* event)
{
	m_width = width();
	m_height = height();
	m_seekerWidth = m_width - s_seekerHorMargin * 2;
	/* m_seekerHeight = m_height * 0.33f; */
	m_editorWidth = m_width;
	m_editorHeight = m_height - m_seekerHeight - s_middleMargin - s_seekerVerMargin;

	m_seeker = QPixmap(m_seekerWidth, m_seekerHeight);
	m_seekerWaveform = QPixmap(m_seekerWidth, m_seekerHeight);
	m_editorWaveform = QPixmap(m_editorWidth, m_editorHeight - s_arrowHeight);
	m_sliceEditor = QPixmap(m_width, m_editorHeight);
	updateUI();
}
void SlicerTWaveform::drawSeekerWaveform()
{
	m_seekerWaveform.fill(s_emptyColor);
	if (m_slicerTParent->m_originalSample.sampleSize() <= 1) { return; }
	QPainter brush(&m_seekerWaveform);
	brush.setPen(s_waveformColor);

	const auto& sample = m_slicerTParent->m_originalSample;

	m_sampleThumbnail = SampleThumbnail{sample};

	const auto param = SampleThumbnail::VisualizeParameters{
		.sampleRect = m_seekerWaveform.rect(),
		.amplification = sample.amplification(),
		.sampleStart = static_cast<float>(sample.startFrame()) / sample.sampleSize(),
		.sampleEnd = static_cast<float>(sample.endFrame()) / sample.sampleSize(),
		.reversed = sample.reversed()
	};

	m_sampleThumbnail.visualize(param, brush);


	// increase brightness in inner color
	QBitmap innerMask = m_seekerWaveform.createMaskFromColor(s_waveformMaskColor, Qt::MaskMode::MaskOutColor);
	brush.setPen(s_waveformInnerColor);
	brush.drawPixmap(0, 0, innerMask);
}

void SlicerTWaveform::drawSeeker()
{
	m_seeker.fill(s_waveformSeekerBgColor);
	if (m_slicerTParent->m_originalSample.sampleSize() <= 1) { return; }
	QPainter brush(&m_seeker);
	brush.drawPixmap(0, 0, m_seekerWaveform);

	brush.setPen(s_sliceColor);
	for (float sliceValue : m_slicerTParent->m_slicePoints)
	{
		float xPos = sliceValue * m_seekerWidth;
		brush.drawLine(xPos, 0, xPos, m_seekerHeight);
	}

	float seekerStartPosX = m_seekerStart * m_seekerWidth;
	float seekerEndPosX = m_seekerEnd * m_seekerWidth;
	float seekerMiddleWidth = (m_seekerEnd - m_seekerStart) * m_seekerWidth;

	float noteCurrentPosX = m_noteCurrent * m_seekerWidth;
	float noteStartPosX = m_noteStart * m_seekerWidth;
	float noteEndPosX = (m_noteEnd - m_noteStart) * m_seekerWidth;

	brush.setPen(s_playColor);
	brush.drawLine(noteCurrentPosX, 0, noteCurrentPosX, m_seekerHeight);
	brush.fillRect(noteStartPosX, 0, noteEndPosX, m_seekerHeight, s_playHighlightColor);

	brush.fillRect(seekerStartPosX, 0, seekerMiddleWidth - 1, m_seekerHeight, s_seekerHighlightColor);

	brush.fillRect(0, 0, seekerStartPosX, m_seekerHeight, s_seekerShadowColor);
	brush.fillRect(seekerEndPosX - 1, 0, m_seekerWidth, m_seekerHeight, s_seekerShadowColor);

	brush.setPen(QPen(s_seekerColor, 1));
	brush.drawRoundedRect(seekerStartPosX, 0, seekerMiddleWidth - 1, m_seekerHeight - 1, 2, 2);
}

void SlicerTWaveform::drawEditorWaveform()
{
	m_editorWaveform.fill(s_emptyColor);
	if (m_slicerTParent->m_originalSample.sampleSize() <= 1) { return; }

	QPainter brush(&m_editorWaveform);
	size_t startFrame = m_seekerStart * m_slicerTParent->m_originalSample.sampleSize();
	size_t endFrame = m_seekerEnd * m_slicerTParent->m_originalSample.sampleSize();

	brush.setPen(s_waveformColor);
	long zoomOffset = (m_editorHeight - m_zoomLevel * m_editorHeight) / 2;

	const auto& sample = m_slicerTParent->m_originalSample;

	m_sampleThumbnail = SampleThumbnail{sample};

	const auto param = SampleThumbnail::VisualizeParameters{
		.sampleRect = QRect(0, zoomOffset, m_editorWidth, static_cast<long>(m_zoomLevel * m_editorHeight)),
		.amplification = sample.amplification(),
		.sampleStart = static_cast<float>(startFrame) / sample.sampleSize(),
		.sampleEnd = static_cast<float>(endFrame) / sample.sampleSize(),
		.reversed = sample.reversed(),
	};

	m_sampleThumbnail.visualize(param, brush);

	// increase brightness in inner color
	QBitmap innerMask = m_editorWaveform.createMaskFromColor(s_waveformMaskColor, Qt::MaskMode::MaskOutColor);
	brush.setPen(s_waveformInnerColor);
	brush.drawPixmap(0, 0, innerMask);
}

void SlicerTWaveform::drawEditor()
{
	m_sliceEditor.fill(s_waveformEditorBgColor);
	QPainter brush(&m_sliceEditor);

	// No sample loaded
	if (m_slicerTParent->m_originalSample.sampleSize() <= 1)
	{
		brush.setPen(s_playHighlightColor);
		brush.setFont(QFont(brush.font().family(), 9.0f, -1, false));
		brush.drawText(
			m_editorWidth / 2 - 100, m_editorHeight / 2 - 110, 200, 200, Qt::AlignCenter, tr("Click to load sample"));
		int iconOffsetX = m_emptySampleIcon.width() / 2.0f;
		int iconOffsetY = m_emptySampleIcon.height() / 2.0f - 13;
		brush.drawPixmap(m_editorWidth / 2.0f - iconOffsetX, m_editorHeight / 2.0f - iconOffsetY, m_emptySampleIcon);
		return;
	}

	float startFrame = m_seekerStart;
	float endFrame = m_seekerEnd;
	float numFramesToDraw = endFrame - startFrame;

	// playback state
	float noteCurrentPos = (m_noteCurrent - m_seekerStart) / (m_seekerEnd - m_seekerStart) * m_editorWidth;
	float noteStartPos = (m_noteStart - m_seekerStart) / (m_seekerEnd - m_seekerStart) * m_editorWidth;
	float noteLength = (m_noteEnd - m_noteStart) / (m_seekerEnd - m_seekerStart) * m_editorWidth;

	brush.setPen(s_playHighlightColor);
	int middleY = m_editorHeight / 2 + s_arrowHeight;
	brush.drawLine(0, middleY, m_editorWidth, middleY);

	brush.drawPixmap(0, s_arrowHeight, m_editorWaveform);

	brush.fillRect(0, 0, m_editorWidth, s_arrowHeight, s_waveformSeekerBgColor);

	brush.setPen(s_playColor);
	brush.drawLine(noteCurrentPos, s_arrowHeight, noteCurrentPos, m_editorHeight);
	brush.fillRect(noteStartPos, s_arrowHeight, noteLength, m_editorHeight, s_playHighlightColor);

	brush.setPen(QPen(s_sliceColor, 2));

	for (auto i = std::size_t{0}; i < m_slicerTParent->m_slicePoints.size(); i++)
	{
		float xPos = (m_slicerTParent->m_slicePoints.at(i) - startFrame) / numFramesToDraw * m_editorWidth;

		if (i == static_cast<std::size_t>(m_closestSlice))
		{
			brush.setPen(QPen(s_sliceHighlightColor, 2));
			brush.drawLine(xPos, 0, xPos, m_editorHeight);
			brush.drawPixmap(xPos - m_sliceArrow.width() / 2.0f, 0, m_sliceArrow);
			continue;
		}
		else
		{
			brush.setPen(QPen(s_sliceShadowColor, 1));
			brush.drawLine(xPos - 1, 0, xPos - 1, m_editorHeight);
			brush.setPen(QPen(s_sliceColor, 1));
			brush.drawLine(xPos, 0, xPos, m_editorHeight);
			brush.drawPixmap(xPos - m_sliceArrow.width() / 2.0f, 0, m_sliceArrow);
		}
	}

	// the key each slice plays, where there is room for it
	const auto& slicePoints = m_slicerTParent->m_slicePoints;
	const int firstKey = m_slicerTParent->firstSliceKey();
	brush.setFont(QFont(brush.font().family(), 7, -1, false));
	for (auto i = std::size_t{0}; i + 1 < slicePoints.size(); i++)
	{
		const float xStart = (slicePoints[i] - startFrame) / numFramesToDraw * m_editorWidth;
		const float xEnd = (slicePoints[i + 1] - startFrame) / numFramesToDraw * m_editorWidth;
		if (xEnd - xStart < s_minKeyLabelWidth || xEnd < 0 || xStart > m_editorWidth) { continue; }

		const int key = firstKey + static_cast<int>(i);
		const bool auditioned = m_auditionKey == key;
		brush.setPen(auditioned ? s_sliceHighlightColor : s_sliceShadowColor);
		brush.drawText(QRectF(xStart + 4, s_arrowHeight + 2, xEnd - xStart - 6, 12), Qt::AlignLeft,
			keyName(key));
	}

	drawRangeHint(brush);

	// decor
	brush.setPen(s_editorBounding);
	brush.drawLine(0, s_arrowHeight, m_editorWidth, s_arrowHeight);
	brush.drawLine(0, m_editorHeight - 1, m_editorWidth, m_editorHeight - 1);
}

void SlicerTWaveform::drawRangeHint(QPainter& brush)
{
	const auto& sample = m_slicerTParent->m_originalSample;

	QString hint;
	if (m_slicerTParent->isSliceRangePartial())
	{
		const float seconds = static_cast<float>(sample.sampleSize()) / std::max(1, sample.sampleRate());
		hint = tr("Slicing again only changes %1s - %2s  |  Middle-click: whole sample")
			.arg(m_seekerStart * seconds, 0, 'f', 2)
			.arg(m_seekerEnd * seconds, 0, 'f', 2);
	}
	else if (m_closestObject == UIObjects::SeekerStart || m_closestObject == UIObjects::SeekerEnd
		|| m_closestObject == UIObjects::SeekerMiddle)
	{
		hint = tr("Drag the edges of this bar to slice only part of the sample");
	}
	if (hint.isEmpty()) { return; }

	brush.setFont(QFont(brush.font().family(), 7, -1, false));
	const QRectF rect(0, m_editorHeight - 15, m_editorWidth, 14);
	const int textWidth = brush.fontMetrics().horizontalAdvance(hint) + 10;
	brush.fillRect(QRectF(0, rect.y(), textWidth, rect.height()), s_seekerShadowColor);
	brush.setPen(s_sliceColor);
	brush.drawText(rect.adjusted(5, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, hint);
}

void SlicerTWaveform::isPlaying(float current, float start, float end)
{
	if (!m_updateTimer.hasExpired(s_minMilisPassed)) { return; }
	m_noteCurrent = current;
	m_noteStart = start;
	m_noteEnd = end;
	drawSeeker();
	drawEditor();
	update();
	m_updateTimer.restart();
}

// this should only be called if one of the waveforms has to update
void SlicerTWaveform::updateUI()
{
	// The range is reset when a new sample is loaded
	m_seekerStart = m_slicerTParent->sliceRangeStart();
	m_seekerEnd = m_slicerTParent->sliceRangeEnd();

	drawSeekerWaveform();
	drawEditorWaveform();
	drawSeeker();
	drawEditor();
	update();
}

// updates the closest object and changes the cursor respectivly
void SlicerTWaveform::updateClosest(QMouseEvent* me)
{
	const auto pos = position(me);

	float normalizedClickSeeker = static_cast<float>(pos.x() - s_seekerHorMargin) / m_seekerWidth;
	float normalizedClickEditor = static_cast<float>(pos.x()) / m_editorWidth;

	m_closestObject = UIObjects::Nothing;
	m_closestSlice = -1;

	if (pos.y() < m_seekerHeight)
	{
		if (std::abs(normalizedClickSeeker - m_seekerStart) < s_distanceForClick)
		{
			m_closestObject = UIObjects::SeekerStart;
		}
		else if (std::abs(normalizedClickSeeker - m_seekerEnd) < s_distanceForClick)
		{
			m_closestObject = UIObjects::SeekerEnd;
		}
		else if (normalizedClickSeeker > m_seekerStart && normalizedClickSeeker < m_seekerEnd)
		{
			m_closestObject = UIObjects::SeekerMiddle;
		}
	}
	else
	{
		m_closestSlice = -1;
		float startFrame = m_seekerStart;
		float endFrame = m_seekerEnd;
		for (auto i = std::size_t{0}; i < m_slicerTParent->m_slicePoints.size(); i++)
		{
			float sliceIndex = m_slicerTParent->m_slicePoints.at(i);
			float xPos = (sliceIndex - startFrame) / (endFrame - startFrame);

			if (std::abs(xPos - normalizedClickEditor) < s_distanceForClick)
			{
				m_closestObject = UIObjects::SlicePoint;
				m_closestSlice = i;
			}
		}
	}
	updateCursor();
	drawSeeker();
	drawEditor();
	update();
}

void SlicerTWaveform::updateCursor()
{
	if (m_closestObject == UIObjects::SlicePoint || m_closestObject == UIObjects::SeekerStart
		|| m_closestObject == UIObjects::SeekerEnd)
	{
		setCursor(Qt::SizeHorCursor);
	}
	else if (m_closestObject == UIObjects::SeekerMiddle && m_seekerEnd - m_seekerStart != 1.0f)
	{
		setCursor(Qt::SizeAllCursor);
	}
	else { setCursor(Qt::ArrowCursor); }
}

// handles deletion, reset and middles seeker
void SlicerTWaveform::mousePressEvent(QMouseEvent* me)
{
	const auto pos = position(me);

	switch (me->button())
	{
	case Qt::MouseButton::MiddleButton:
		setSeekerRange(0, 1);
		m_zoomLevel = 1;
		drawEditorWaveform();
		break;
	case Qt::MouseButton::LeftButton:
		if (m_slicerTParent->m_originalSample.sampleSize() <= 1)
		{
			static_cast<SlicerTView*>(parent())->openFiles();
			break;
		}
		// Alt-click plays the slice under the cursor until the button is released
		if (me->modifiers() & Qt::AltModifier && pos.y() >= m_seekerHeight)
		{
			auditionSlice(m_slicerTParent->sliceAt(editorToSample(pos.x())));
			updateSlices();
			return;
		}
		// update seeker middle for correct movement
		m_seekerMiddle = static_cast<float>(pos.x() - s_seekerHorMargin) / m_seekerWidth;
		m_sliceMoved = false;
		break;
	case Qt::MouseButton::RightButton:
		if (m_closestObject == UIObjects::SlicePoint)
		{
			m_slicerTParent->removeSlicePoint(m_closestSlice);
			updateSlices();
		}
		break;
	default:;
	}
	updateClosest(me);
}

// sort slices after moving and remove draggable object
void SlicerTWaveform::mouseReleaseEvent(QMouseEvent* me)
{
	if (m_auditionKey >= 0)
	{
		stopAudition();
		updateSlices();
	}
	if (m_sliceMoved)
	{
		m_slicerTParent->finishSliceEdit();
		m_sliceMoved = false;
	}
	updateClosest(me);
}

// this handles dragging and mouse cursor changes
// what is being dragged is determined in mousePressEvent
void SlicerTWaveform::mouseMoveEvent(QMouseEvent* me)
{
	// if no button pressed, update closest and cursor
	if (me->buttons() == Qt::MouseButton::NoButton)
	{
		updateClosest(me);
		return;
	}

	// auditioning holds the slice until release; dragging must not edit anything
	if (m_auditionKey >= 0) { return; }

	const auto pos = position(me);

	float normalizedClickSeeker = static_cast<float>(pos.x() - s_seekerHorMargin) / m_seekerWidth;

	float distStart = m_seekerStart - m_seekerMiddle;
	float distEnd = m_seekerEnd - m_seekerMiddle;

	switch (m_closestObject)
	{
	case UIObjects::SeekerStart:
		setSeekerRange(std::clamp(normalizedClickSeeker, 0.0f, m_seekerEnd - s_minSeekerDistance), m_seekerEnd);
		drawEditorWaveform();
		break;

	case UIObjects::SeekerEnd:
		setSeekerRange(m_seekerStart, std::clamp(normalizedClickSeeker, m_seekerStart + s_minSeekerDistance, 1.0f));
		drawEditorWaveform();
		break;

	case UIObjects::SeekerMiddle:
		m_seekerMiddle = normalizedClickSeeker;

		if (m_seekerMiddle + distStart >= 0 && m_seekerMiddle + distEnd <= 1)
		{
			setSeekerRange(m_seekerMiddle + distStart, m_seekerMiddle + distEnd);
		}
		drawEditorWaveform();
		break;

	case UIObjects::SlicePoint:
		if (m_closestSlice == -1) { break; }
		m_slicerTParent->moveSlicePoint(m_closestSlice, editorToSample(pos.x()));
		m_sliceMoved = true;
		break;
	case UIObjects::Nothing:
		break;
	}
	// dont update closest, and update seeker waveform
	drawSeeker();
	drawEditor();
	update();
}

void SlicerTWaveform::mouseDoubleClickEvent(QMouseEvent* me)
{
	const auto pos = position(me);

	if (me->button() != Qt::MouseButton::LeftButton || me->modifiers() & Qt::AltModifier
		|| pos.y() < m_seekerHeight || m_slicerTParent->m_originalSample.sampleSize() <= 1)
	{
		return;
	}

	m_slicerTParent->addSlicePoint(editorToSample(pos.x()));
	updateSlices();
}

void SlicerTWaveform::focusOutEvent(QFocusEvent* fe)
{
	stopAudition();
	QWidget::focusOutEvent(fe);
}

void SlicerTWaveform::leaveEvent(QEvent* e)
{
	if (QApplication::mouseButtons() == Qt::NoButton)
	{
		m_closestObject = UIObjects::Nothing;
		m_closestSlice = -1;
		updateSlices();
	}
	QWidget::leaveEvent(e);
}

void SlicerTWaveform::setSeekerRange(float start, float end)
{
	m_seekerStart = start;
	m_seekerEnd = end;
	m_slicerTParent->setSliceRange(start, end);
}

void SlicerTWaveform::updateSlices()
{
	drawSeeker();
	drawEditor();
	update();
}

float SlicerTWaveform::editorToSample(int x) const
{
	return m_seekerStart + static_cast<float>(x) / m_editorWidth * (m_seekerEnd - m_seekerStart);
}

void SlicerTWaveform::auditionSlice(int index)
{
	stopAudition();
	if (index < 0) { return; }
	InstrumentTrack* track = m_slicerTParent->m_parentTrack;
	const int key = m_slicerTParent->firstSliceKey() + index;
	if (key >= NumKeys) { return; }
	m_auditionKey = key;
	track->processInEvent(MidiEvent(MidiNoteOn, -1, m_auditionKey, MidiDefaultVelocity));
}

void SlicerTWaveform::stopAudition()
{
	if (m_auditionKey < 0) { return; }
	m_slicerTParent->m_parentTrack->processInEvent(MidiEvent(MidiNoteOff, -1, m_auditionKey, 0));
	m_auditionKey = -1;
}

void SlicerTWaveform::wheelEvent(QWheelEvent* we)
{
	m_zoomLevel += we->angleDelta().y() / 360.0f * s_zoomSensitivity;
	m_zoomLevel = std::max(0.0f, m_zoomLevel);

	updateUI();
}

void SlicerTWaveform::paintEvent(QPaintEvent* pe)
{
	QPainter p(this);

	// top gradient
	QLinearGradient bgGrad(QPointF(0, 0), QPointF(width(), height() - m_editorHeight));
	bgGrad.setColorAt(0, s_waveformEditorBgColor);
	bgGrad.setColorAt(1, s_gradientEnd);

	p.setBrush(bgGrad);
	p.setPen(s_emptyColor);
	p.drawRect(QRect(0, 0, width(), height()));
	p.setBrush(QBrush());

	// seeker
	QPainterPath path;
	path.addRoundedRect(
		QRect(s_seekerHorMargin - 2, s_seekerVerMargin - 2, m_seekerWidth + 4, m_seekerHeight + 4), 4, 4);
	p.fillPath(path, s_seekerShadowColor);
	p.drawPixmap(s_seekerHorMargin, s_seekerVerMargin, m_seeker);

	// editor
	p.setPen(QColor(s_waveformColor));
	p.drawPixmap(0, m_seekerHeight + s_middleMargin + s_seekerVerMargin, m_sliceEditor);
}
} // namespace gui
} // namespace lmms
