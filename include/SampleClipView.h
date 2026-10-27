/*
 * SampleClipView.h
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

#ifndef LMMS_GUI_SAMPLE_CLIP_VIEW_H
#define LMMS_GUI_SAMPLE_CLIP_VIEW_H

#include "ClipView.h"

#include "SampleThumbnail.h"

namespace lmms
{

class SampleClip;

namespace gui
{


class SampleClipView : public ClipView
{
	Q_OBJECT

public:
	SampleClipView( SampleClip * _clip, TrackView * _tv );
	~SampleClipView() override = default;

public slots:
	void updateSample();
	void reverseSample();
	//! Dialog for typing fade lengths and the clip gain
	void editFadesAndGain();
	//! Dialog for typing the sample's original and target BPM, then resampling or time-stretching it to match
	void resampleBpm();
	void setAutomationGhost();



protected:
	void constructContextMenu(QMenu* cm) override;
	void mousePressEvent( QMouseEvent * _me ) override;
	void mouseMoveEvent( QMouseEvent * _me ) override;
	void mouseReleaseEvent( QMouseEvent * _me ) override;
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	void enterEvent(QEnterEvent* _e) override;
#else
	void enterEvent( QEvent * _e ) override;
#endif
	void leaveEvent( QEvent * _e ) override;
	void dragEnterEvent( QDragEnterEvent * _dee ) override;
	void dropEvent( QDropEvent * _de ) override;
	void mouseDoubleClickEvent( QMouseEvent * ) override;
	void paintEvent( QPaintEvent * ) override;


private:
	//! The draggable handles on top of the clip
	enum class Handle { None, FadeIn, FadeOut, Gain };

	float pixelsPerTick() const;
	QPointF handlePos(Handle handle) const;
	Handle handleAt(const QPoint& pos);
	void showHandleValue(Handle handle);
	void paintFades(QPainter& p, int xOffset);

	Handle m_dragHandle = Handle::None;
	Handle m_hoverHandle = Handle::None;
	bool m_hovered = false;
	int m_dragStartY = 0;
	float m_dragStartGainDb = 0.f;

	SampleClip * m_clip;
	SampleThumbnail m_sampleThumbnail;
	QPixmap m_paintPixmap;
	long m_paintPixmapXPosition;
} ;


} // namespace gui

} // namespace lmms

#endif // LMMS_GUI_SAMPLE_CLIP_VIEW_H
