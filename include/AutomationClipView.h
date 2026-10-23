/*
 * AutomationClipView.h - declaration of class AutomationClipView
 *
 * Copyright (c) 2008-2010 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef LMMS_GUI_AUTOMATION_CLIP_VIEW_H
#define LMMS_GUI_AUTOMATION_CLIP_VIEW_H

#include <optional>
#include <QStaticText>

#include "AutomationClip.h"
#include "ClipView.h"

namespace lmms
{

namespace gui
{


class AutomationClipView : public ClipView
{
	Q_OBJECT


public:
	AutomationClipView( AutomationClip * _clip, TrackView * _parent );
	~AutomationClipView() override = default;

public slots:
	/// Opens this view's clip in the global automation editor
	void openInAutomationEditor();
	void update() override;


protected slots:
	void resetName();
	void changeName();
	void disconnectObject( QAction * _a );
	void toggleRecording();
	void flipY();
	void flipX();
	void setProgressionType(lmms::AutomationClip::ProgressionType type);

protected:
	void constructContextMenu( QMenu * ) override;
	void contextMenuEvent(QContextMenuEvent* cme) override;
	void mousePressEvent(QMouseEvent* me) override;
	void mouseMoveEvent(QMouseEvent* me) override;
	void mouseReleaseEvent(QMouseEvent* me) override;
	void mouseDoubleClickEvent(QMouseEvent * me ) override;
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	void enterEvent(QEnterEvent* e) override;
#else
	void enterEvent(QEvent* e) override;
#endif
	void leaveEvent(QEvent* e) override;
	void paintEvent( QPaintEvent * pe ) override;
	void dragEnterEvent( QDragEnterEvent * _dee ) override;
	void dropEvent( QDropEvent * _de ) override;


private:
	AutomationClip * m_clip;
	QPixmap m_paintPixmap;
	
	QStaticText m_staticTextName;

	//! @brief Preserves the auto points over different scale
	void scaleTimemapToFit( float oldMin, float oldMax );
	
	bool isResizableBeforeStart() override { return false; }

	// Editing the automation points right on the clip, without opening the Automation Editor
	float pixelsPerTick() const;
	float xOfTick(int tick) const;
	int tickAtX(float x) const;
	float yOfValue(float value) const;
	//! The value at @p y, snapped to the model's step and kept in range
	float valueAtY(float y) const;
	//! @p value snapped to the model's step and kept in range
	float snapValue(float value) const;
	bool canEditNodes() const;
	//! The time of the node drawn under @p pos, if any
	std::optional<int> nodeAt(const QPoint& pos) const;
	//! Whether @p pos is on the drawn curve, where a click adds a node
	bool isOnCurve(const QPoint& pos) const;
	void updateHover(const QPoint& pos);
	void showNodeValue(int tick);
	void paintNodes(QPainter& p);

	bool m_hovered = false;
	std::optional<int> m_hoverNode;
	bool m_hoverCurve = false;
	std::optional<int> m_dragNode;
	QPoint m_dragOffset;
	//! Values are dragged relative to where the drag started, at reduced sensitivity
	float m_dragStartValue = 0.f;
	int m_dragStartY = 0;
	//! A right-click removed a node, so the context menu that follows it is not wanted
	bool m_skipContextMenu = false;
} ;


} // namespace gui

} // namespace lmms

#endif // LMMS_GUI_AUTOMATION_CLIP_VIEW_H
