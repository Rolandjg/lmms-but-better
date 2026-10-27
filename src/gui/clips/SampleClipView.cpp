/*
 * SampleClipView.cpp
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
 
#include "SampleClipView.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProgressDialog>
#include <QRegularExpression>

#include "ConfigManager.h"
#include "FileDialog.h"
#include "FontHelper.h"
#include "GuiApplication.h"
#include "AutomationEditor.h"
#include "AudioEngine.h"
#include "embed.h"
#include "lmms_math.h"
#include "PathUtil.h"
#include "SampleClip.h"
#include "SampleThumbnail.h"
#include "Song.h"
#include "StringPairDrag.h"
#include "TextFloat.h"
#include "TrackContainerView.h"
#include "TrackView.h"

namespace lmms::gui
{

namespace
{

constexpr int HandleRadius = 5;
constexpr int HandleHitRadius = 10;
//! Handles live in this band at the top of the clip, so the edges below still resize the clip
constexpr int HandleBand = 16;
//! The gain handle sits between the fade handles and needs room to stay clear of them
constexpr int MinWidthForGainHandle = 4 * HandleHitRadius;
constexpr float MinGainDb = -48.f;
constexpr float MaxGainDb = 24.f;

QString gainText(float gain)
{
	if (gain <= dbfsToAmp(MinGainDb)) { return QStringLiteral("-inf dB"); }
	const float db = ampToDbfs(gain);
	return QString("%1%2 dB").arg(db > 0.05f ? "+" : "").arg(db, 0, 'f', 1);
}

} // namespace


SampleClipView::SampleClipView( SampleClip * _clip, TrackView * _tv ) :
	ClipView( _clip, _tv ),
	m_clip( _clip ),
	m_paintPixmap(),
	m_paintPixmapXPosition(0)
{
	// update UI and tooltip
	updateSample();

	// track future changes of SampleClip
	connect(m_clip, SIGNAL(sampleChanged()), this, SLOT(updateSample()));

	connect(m_clip, SIGNAL(wasReversed()), this, SLOT(update()));

	setStyle( QApplication::style() );
}

void SampleClipView::updateSample()
{
	update();

	m_sampleThumbnail = SampleThumbnail{m_clip->m_sample};

	// set tooltip to filename so that user can see what sample this
	// sample-clip contains
	setToolTip(
		!m_clip->m_sample.sampleFile().isEmpty()
			? PathUtil::toAbsolute(m_clip->m_sample.sampleFile())
			: tr("Double-click to open sample")
	);
}




void SampleClipView::constructContextMenu(QMenu* cm)
{
	cm->addSeparator();

	/*contextMenu.addAction( embed::getIconPixmap( "record" ),
				tr( "Set/clear record" ),
						m_clip, SLOT(toggleRecord()));*/

	cm->addAction(
		embed::getIconPixmap("flip_x"),
		tr("Reverse sample"),
		this,
		SLOT(reverseSample())
	);
	auto resample = cm->addAction(tr("Resample BPM…"), this, &SampleClipView::resampleBpm);
	resample->setEnabled(m_clip->m_sample.sampleSize() > 0);

	cm->addSeparator();
	cm->addAction(tr("Fades and gain…"), this, &SampleClipView::editFadesAndGain);
	cm->addAction(tr("Normalize clip gain"), this, [this] {
		m_clip->addJournalCheckPoint();
		m_clip->normalize();
	});
	auto resetGain = cm->addAction(tr("Reset clip gain (%1)").arg(gainText(m_clip->gain())), this, [this] {
		m_clip->addJournalCheckPoint();
		m_clip->setGain(1.f);
	});
	resetGain->setEnabled(m_clip->gain() != 1.f);
	auto removeFades = cm->addAction(tr("Remove fades"), this, [this] {
		m_clip->addJournalCheckPoint();
		m_clip->setFadeIn(0);
		m_clip->setFadeOut(0);
	});
	removeFades->setEnabled(m_clip->fadeIn() > 0 || m_clip->fadeOut() > 0);
	cm->addSeparator();

	cm->addAction(
		embed::getIconPixmap("automation_ghost_note"),
		tr("Set as ghost in automation editor"),
		this,
		SLOT(setAutomationGhost())
	);

}




void SampleClipView::dragEnterEvent( QDragEnterEvent * _dee )
{
	if( StringPairDrag::processDragEnterEvent( _dee,
					"samplefile,sampledata" ) == false )
	{
		ClipView::dragEnterEvent( _dee );
	}
}






void SampleClipView::dropEvent( QDropEvent * _de )
{
	if( StringPairDrag::decodeKey( _de ) == "samplefile" )
	{
		m_clip->setSampleFile( StringPairDrag::decodeValue( _de ) );
		_de->accept();
	}
	else if( StringPairDrag::decodeKey( _de ) == "sampledata" )
	{
		m_clip->setSampleBuffer(SampleBuffer::fromBase64(StringPairDrag::decodeValue(_de)));
		m_clip->updateLength();
		update();
		_de->accept();
	}
	else
	{
		ClipView::dropEvent( _de );
	}
}




void SampleClipView::editFadesAndGain()
{
	// The same edits as the handles, typed in: for precise values, the keyboard, and clips too short to grab
	const float ticksPerBeat = static_cast<float>(DefaultTicksPerBar) / Engine::getSong()->getTimeSigModel().getDenominator();
	const float lengthBeats = m_clip->length() / ticksPerBeat;

	QDialog dialog(this);
	dialog.setWindowTitle(tr("Fades and gain"));
	auto layout = new QFormLayout(&dialog);
	const auto beatsBox = [&](TimePos value) {
		auto box = new QDoubleSpinBox(&dialog);
		box->setRange(0., lengthBeats);
		box->setDecimals(2);
		box->setSingleStep(0.25);
		box->setSuffix(tr(" beats"));
		box->setValue(value / ticksPerBeat);
		return box;
	};
	auto fadeIn = beatsBox(m_clip->fadeIn());
	auto fadeOut = beatsBox(m_clip->fadeOut());
	auto gain = new QDoubleSpinBox(&dialog);
	gain->setRange(MinGainDb, MaxGainDb);
	gain->setDecimals(1);
	gain->setSingleStep(0.5);
	gain->setSuffix(tr(" dB"));
	gain->setSpecialValueText(tr("-inf dB"));
	gain->setValue(m_clip->gain() <= dbfsToAmp(MinGainDb) ? MinGainDb : ampToDbfs(m_clip->gain()));
	layout->addRow(tr("Fade in:"), fadeIn);
	layout->addRow(tr("Fade out:"), fadeOut);
	layout->addRow(tr("Clip gain:"), gain);
	auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addRow(buttons);
	if (dialog.exec() != QDialog::Accepted) { return; }

	m_clip->addJournalCheckPoint();
	m_clip->setFadeIn(static_cast<tick_t>(std::lround(fadeIn->value() * ticksPerBeat)));
	m_clip->setFadeOut(static_cast<tick_t>(std::lround(fadeOut->value() * ticksPerBeat)));
	m_clip->setGain(gain->value() <= MinGainDb ? 0.f : dbfsToAmp(static_cast<float>(gain->value())));
	update();
}




void SampleClipView::resampleBpm()
{
	const double songBpm = Engine::getSong()->getTempo();

	// Loops are often named after their tempo ("drums_128bpm.wav"), so start from that when we can
	double originalBpm = songBpm;
	static const auto bpmInName = QRegularExpression(R"((\d{2,3}(?:\.\d+)?)\s*bpm)", QRegularExpression::CaseInsensitiveOption);
	if (const auto match = bpmInName.match(PathUtil::cleanName(m_clip->sampleFile())); match.hasMatch())
	{
		originalBpm = match.captured(1).toDouble();
	}

	QDialog dialog(this);
	dialog.setWindowTitle(tr("Resample BPM"));
	auto layout = new QFormLayout(&dialog);
	const auto bpmBox = [&](double value) {
		auto box = new QDoubleSpinBox(&dialog);
		box->setRange(MinTempo, MaxTempo);
		box->setDecimals(2);
		box->setSuffix(tr(" BPM"));
		box->setValue(value);
		return box;
	};
	auto original = bpmBox(originalBpm);
	auto target = bpmBox(songBpm);
	layout->addRow(tr("Original BPM:"), original);
	layout->addRow(tr("New BPM:"), target);
	auto preservePitch = new QCheckBox(tr("Preserve pitch (time-stretch)"), &dialog);
	preservePitch->setToolTip(tr("Change only the tempo. When unchecked the sample is sped up or slowed down "
		"like a turntable, which also changes its pitch."));
	preservePitch->setChecked(ConfigManager::inst()->value("ui", "resamplepreservepitch", "1").toInt());
	layout->addRow(preservePitch);
	auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addRow(buttons);
	original->setFocus();
	original->selectAll();
	if (dialog.exec() != QDialog::Accepted) { return; }
	ConfigManager::inst()->setValue("ui", "resamplepreservepitch", QString::number(preservePitch->isChecked()));
	if (original->value() == target->value()) { return; }

	// Stretching a long sample takes a while, so show how far along it is
	QProgressDialog progress(tr("Time-stretching %1…").arg(PathUtil::cleanName(m_clip->sampleFile())), QString(), 0, 1000, this);
	progress.setWindowTitle(tr("Resample BPM"));
	progress.setWindowModality(Qt::ApplicationModal);
	progress.setMinimumDuration(500);

	m_clip->addJournalCheckPoint();
	m_clip->resample(target->value() / original->value(), preservePitch->isChecked(), [&progress](float done) {
		progress.setValue(static_cast<int>(done * progress.maximum()));
	});
	progress.reset();
	update();
}




float SampleClipView::pixelsPerTick() const
{
	return pixelsPerBar() / TimePos::ticksPerBar();
}




QPointF SampleClipView::handlePos(Handle handle) const
{
	const float ppt = pixelsPerTick();
	const float inset = HandleRadius + 2.f;
	const float y = HandleRadius + 2.f;
	switch (handle)
	{
	case Handle::FadeIn:
		return {std::clamp(m_clip->fadeIn() * ppt, inset, width() - inset), y};
	case Handle::FadeOut:
		return {std::clamp(width() - m_clip->fadeOut() * ppt, inset, width() - inset), y};
	case Handle::Gain:
		return {width() / 2.f, y};
	default:
		return {};
	}
}




SampleClipView::Handle SampleClipView::handleAt(const QPoint& pos)
{
	if (fixedClips() || m_trackView->trackContainerView()->knifeMode() || pos.y() > HandleBand) { return Handle::None; }

	// Prefer the handle the cursor is closest to, so the fade handles stay reachable on short clips
	auto best = Handle::None;
	float bestDistance = HandleHitRadius;
	const bool roomForGain = width() > MinWidthForGainHandle;
	for (auto handle : {Handle::FadeIn, Handle::FadeOut, Handle::Gain})
	{
		if (handle == Handle::Gain && !roomForGain) { continue; }
		const float distance = std::abs(handlePos(handle).x() - pos.x());
		if (distance <= bestDistance)
		{
			best = handle;
			bestDistance = distance;
		}
	}
	return best;
}




void SampleClipView::showHandleValue(Handle handle)
{
	auto bubble = textFloat();
	const auto ticks = [](TimePos t) {
		const float beats = t * Engine::getSong()->getTimeSigModel().getDenominator() / float(DefaultTicksPerBar);
		const float ms = t * Engine::framesPerTick() * 1000.f / Engine::audioEngine()->outputSampleRate();
		return tr("%1 beats (%2 ms)").arg(beats, 0, 'f', 2).arg(ms, 0, 'f', 0);
	};
	switch (handle)
	{
	case Handle::FadeIn:
		bubble->setTitle(tr("Fade in"));
		bubble->setText(ticks(m_clip->fadeIn()));
		break;
	case Handle::FadeOut:
		bubble->setTitle(tr("Fade out"));
		bubble->setText(ticks(m_clip->fadeOut()));
		break;
	case Handle::Gain:
		bubble->setTitle(tr("Clip gain"));
		bubble->setText(gainText(m_clip->gain()));
		break;
	default:
		return;
	}
	bubble->moveGlobal(this, QPoint(width() + 2, height() + 2));
	bubble->show();
}




void SampleClipView::mousePressEvent( QMouseEvent * _me )
{
	if (_me->button() == Qt::LeftButton && !(_me->modifiers() & (Qt::ControlModifier | Qt::AltModifier)))
	{
		if (const auto handle = handleAt(_me->pos()); handle != Handle::None)
		{
			m_clip->addJournalCheckPoint();
			m_dragHandle = handle;
			m_dragStartY = _me->pos().y();
			m_dragStartGainDb = std::max(MinGainDb, ampToDbfs(std::max(m_clip->gain(), 1e-6f)));
			showHandleValue(handle);
			update();
			return;
		}
	}

	if( _me->button() == Qt::LeftButton &&
		_me->modifiers() & Qt::ControlModifier &&
		_me->modifiers() & Qt::ShiftModifier )
	{
		m_clip->toggleRecord();
	}
	else
	{
		if( _me->button() == Qt::MiddleButton && _me->modifiers() == Qt::ControlModifier )
		{
			auto sClip = dynamic_cast<SampleClip*>(getClip());
			if( sClip )
			{
				sClip->updateTrackClips();
			}
		}
		ClipView::mousePressEvent( _me );
	}
}




void SampleClipView::mouseMoveEvent(QMouseEvent* me)
{
	if (m_dragHandle != Handle::None)
	{
		const float ppt = pixelsPerTick();
		const int x = me->pos().x();
		switch (m_dragHandle)
		{
		case Handle::FadeIn:
			m_clip->setFadeIn(static_cast<int>(std::round(std::max(0, x) / ppt)));
			break;
		case Handle::FadeOut:
			m_clip->setFadeOut(static_cast<int>(std::round(std::max(0, width() - x) / ppt)));
			break;
		case Handle::Gain:
		{
			// Drag up for louder; hold shift for fine adjustments
			const float dbPerPixel = me->modifiers() & Qt::ShiftModifier ? 0.05f : 0.25f;
			const float db = std::clamp(m_dragStartGainDb + (m_dragStartY - me->pos().y()) * dbPerPixel, MinGainDb, MaxGainDb);
			m_clip->setGain(db <= MinGainDb ? 0.f : dbfsToAmp(db));
			break;
		}
		default:
			break;
		}
		showHandleValue(m_dragHandle);
		return;
	}

	ClipView::mouseMoveEvent(me);

	if (!me->buttons())
	{
		const auto handle = handleAt(me->pos());
		if (handle != m_hoverHandle)
		{
			m_hoverHandle = handle;
			update();
		}
		if (handle == Handle::Gain) { setCursor(Qt::SizeVerCursor); }
		else if (handle != Handle::None) { setCursor(Qt::SizeHorCursor); }
	}
}




#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
void SampleClipView::enterEvent(QEnterEvent* e)
#else
void SampleClipView::enterEvent(QEvent* e)
#endif
{
	m_hovered = true;
	update();
	ClipView::enterEvent(e);
}




void SampleClipView::leaveEvent(QEvent* e)
{
	m_hovered = false;
	m_hoverHandle = Handle::None;
	update();
	ClipView::leaveEvent(e);
}




void SampleClipView::mouseReleaseEvent(QMouseEvent *_me)
{
	if (m_dragHandle != Handle::None)
	{
		m_dragHandle = Handle::None;
		textFloat()->hide();
		update();
		return;
	}

	if( _me->button() == Qt::MiddleButton && !_me->modifiers() )
	{
		auto sClip = dynamic_cast<SampleClip*>(getClip());
		if( sClip )
		{
			sClip->playbackPositionChanged();
		}
	}
	ClipView::mouseReleaseEvent( _me );
}




void SampleClipView::mouseDoubleClickEvent( QMouseEvent * me )
{
	if (m_trackView->trackContainerView()->knifeMode()) { return; }

	// Double-clicking a handle resets it
	switch (handleAt(me->pos()))
	{
	case Handle::FadeIn:
		m_clip->addJournalCheckPoint();
		m_clip->setFadeIn(0);
		return;
	case Handle::FadeOut:
		m_clip->addJournalCheckPoint();
		m_clip->setFadeOut(0);
		return;
	case Handle::Gain:
		m_clip->addJournalCheckPoint();
		m_clip->setGain(1.f);
		return;
	default:
		break;
	}

	const QString selectedAudioFile = FileDialog::openAudioFile();

	if (selectedAudioFile.isEmpty()) { return; }
	
	if (!m_clip->hasSampleFileLoaded(selectedAudioFile))
	{
		auto sampleBuffer = SampleBuffer::fromFile(selectedAudioFile);
		if (sampleBuffer != SampleBuffer::emptyBuffer())
		{
			m_clip->setSampleBuffer(sampleBuffer);
		}
	}
	m_clip->updateLength();
}




void SampleClipView::paintEvent( QPaintEvent * pe )
{
	QPainter painter( this );

	if( !needsUpdate() )
	{
		painter.drawPixmap(m_paintPixmapXPosition, 0, m_paintPixmap);
		return;
	}

	setNeedsUpdate( false );

	const auto trackViewWidth = getTrackView()->rect().width();

	// Use the clip's height to avoid artifacts when rendering while something else is overlaying the clip.
	const auto viewPortRect = QRect(0, 0, trackViewWidth * 2, rect().height());

	m_paintPixmapXPosition = std::max(0, pe->rect().x() - trackViewWidth);

	if (m_paintPixmap.isNull() || m_paintPixmap.size() != viewPortRect.size())
	{
		m_paintPixmap = QPixmap(viewPortRect.size());
	}

	QPainter p( &m_paintPixmap );

	bool muted = m_clip->getTrack()->isMuted() || m_clip->isMuted();
	bool selected = isSelected();

	QLinearGradient lingrad(0, 0, 0, height());
	QColor c = painter.background().color();
	if (muted) { c = c.darker(150); }
	if (selected) { c = c.darker(150); }

	lingrad.setColorAt( 1, c.darker( 300 ) );
	lingrad.setColorAt( 0, c );

	const QRect body(-m_paintPixmapXPosition, 0, width(), height());
	beginClipPaint(p, m_paintPixmap, body);

	// paint a black rectangle under the clip to prevent glitches with transparent backgrounds
	p.fillRect( rect(), QColor( 0, 0, 0 ) );

	if( gradient() )
	{
		p.fillRect( rect(), lingrad );
	}
	else
	{
		p.fillRect( rect(), c );
	}

	auto clipColor = m_clip->color().value_or(m_clip->getTrack()->color().value_or(painter.pen().brush().color()));

	p.setPen(clipColor);

	if (muted)
	{
		QColor penColor = p.pen().brush().color();
		penColor.setHsv(penColor.hsvHue(), penColor.hsvSaturation() / 4, penColor.value());
		p.setPen(penColor.darker(250));
	}
	if (selected)
	{
		p.setPen(p.pen().brush().color().darker(150));
	}

	const int spacing = BORDER_WIDTH + 1;
	const float ppb = fixedClips() ?
			( parentWidget()->width() - 2 * BORDER_WIDTH )
					/ (float) m_clip->length().getBar() :
								pixelsPerBar();

	float nom = Engine::getSong()->getTimeSigModel().getNumerator();
	float den = Engine::getSong()->getTimeSigModel().getDenominator();
	float ticksPerBar = DefaultTicksPerBar * nom / den;
	float offsetStart = m_clip->startTimeOffset() / ticksPerBar * pixelsPerBar();
	float sampleLength = m_clip->sampleLength() * ppb / ticksPerBar;

	const auto& sample = m_clip->m_sample;

	const auto sampleRextX = static_cast<int>(offsetStart) - m_paintPixmapXPosition;

	if (sample.sampleSize() > 0)
	{
		const auto param = SampleThumbnail::VisualizeParameters{
			.sampleRect = QRect(sampleRextX, spacing, sampleLength, height() - spacing),
			.viewportRect = viewPortRect,
			.amplification = sample.amplification(),
			.reversed = sample.reversed()
		};

		m_sampleThumbnail.visualize(param, p);
	}

	paintFades(p, -m_paintPixmapXPosition);

	QString name = PathUtil::cleanName(m_clip->m_sample.sampleFile());
	paintTextLabel(name, p);

	paintClipFrame(p, body, c, false);

	// draw the 'muted' pixmap only if the clip was manually muted
	if( m_clip->isMuted() )
	{
		const int spacing = BORDER_WIDTH;
		const int size = 14;
		p.drawPixmap( spacing, height() - ( size + spacing ),
			embed::getIconPixmap( "muted", size, size ) );
	}

	if ( m_marker )
	{
		p.setPen(markerColor());
		p.drawLine(m_markerPos, rect().bottom(), m_markerPos, rect().top());
	}
	// recording sample tracks is not possible at the moment

	/* if( m_clip->isRecord() )
	{
		p.setFont( pointSize<7>( p.font() ) );

		p.setPen( textShadowColor() );
		p.drawText( 10, p.fontMetrics().height()+1, "Rec" );
		p.setPen( textColor() );
		p.drawText( 9, p.fontMetrics().height(), "Rec" );

		p.setBrush( QBrush( textColor() ) );
		p.drawEllipse( 4, 5, 4, 4 );
	}*/

	p.end();

	painter.drawPixmap(m_paintPixmapXPosition, 0, m_paintPixmap);
}




void SampleClipView::paintFades(QPainter& p, int xOffset)
{
	if (fixedClips()) { return; }

	const float ppt = pixelsPerTick();
	const float w = width();
	const float fadeIn = m_clip->fadeIn() * ppt;
	const float fadeOut = m_clip->fadeOut() * ppt;
	const float top = BORDER_WIDTH;
	const float bottom = height() - BORDER_WIDTH;

	p.save();
	p.translate(xOffset, 0);
	p.setRenderHint(QPainter::Antialiasing);

	// Shade the part of the clip the fades silence and outline the fade curves
	const auto shade = [&](float from, float to) {
		if (to - from < 1.f) { return; }
		QPainterPath curve;
		QPainterPath shadow;
		shadow.moveTo(from, top);
		for (float x = from; x <= to + 0.5f; x += 1.f)
		{
			const float clampedX = std::min(x, to);
			const float y = bottom - SampleClip::fadeGain(clampedX, w, fadeIn, fadeOut) * (bottom - top);
			if (x == from) { curve.moveTo(clampedX, y); }
			else { curve.lineTo(clampedX, y); }
			shadow.lineTo(clampedX, y);
		}
		shadow.lineTo(to, top);
		shadow.closeSubpath();
		p.fillPath(shadow, QColor(0, 0, 0, 150));
		p.strokePath(curve, QPen(QColor(255, 255, 255, 190), 1.3));
	};
	// Overlapping fades are scaled down so they meet in the middle, like the audio does
	const float scale = fadeIn + fadeOut > w && w > 0.f ? w / (fadeIn + fadeOut) : 1.f;
	shade(0.f, fadeIn * scale);
	shade(w - fadeOut * scale, w);

	const bool dragging = m_dragHandle != Handle::None;
	if (m_hovered || dragging)
	{
		const auto drawHandle = [&](Handle handle, bool round) {
			const bool active = m_hoverHandle == handle || m_dragHandle == handle;
			const QPointF c = handlePos(handle);
			p.setPen(QPen(QColor(0, 0, 0, 200), 1));
			p.setBrush(active ? QColor(29, 226, 118) : QColor(240, 244, 248, 230));
			const QRectF r(c.x() - HandleRadius, c.y() - HandleRadius, 2 * HandleRadius, 2 * HandleRadius);
			if (round) { p.drawEllipse(r); }
			else { p.drawRect(r); }
		};
		drawHandle(Handle::FadeIn, false);
		drawHandle(Handle::FadeOut, false);
		if (width() > MinWidthForGainHandle) { drawHandle(Handle::Gain, true); }
	}

	// Show a non-default clip gain as a small badge in the lower right corner
	if (m_clip->gain() != 1.f && width() > 60)
	{
		const QString text = gainText(m_clip->gain());
		p.setFont(adjustedToPixelSize(p.font(), 9));
		const QFontMetrics fm(p.font());
		const QRectF badge(w - fm.horizontalAdvance(text) - 12, bottom - fm.height() - 3, fm.horizontalAdvance(text) + 8,
			fm.height() + 1);
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(0, 0, 0, 150));
		p.drawRect(badge);
		p.setPen(m_clip->gain() > 1.f ? QColor(255, 196, 92) : QColor(220, 226, 234));
		p.drawText(badge, Qt::AlignCenter, text);
	}

	p.restore();
}




void SampleClipView::reverseSample()
{
	m_clip->m_sample.setReversed(!m_clip->m_sample.reversed());
	m_clip->setStartTimeOffset(m_clip->length() - m_clip->startTimeOffset() - m_clip->sampleLength());
	Engine::getSong()->setModified();
	update();
}



void SampleClipView::setAutomationGhost()
{
	auto aEditor = gui::getGUI()->automationEditor();
	aEditor->setGhostSample(m_clip);
	aEditor->parentWidget()->show();
	aEditor->show();
	aEditor->setFocus();
}

} // namespace lmms::gui
