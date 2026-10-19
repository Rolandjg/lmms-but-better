/*
 * TilingWindowManager.h - arranges the workspace subwindows as tiles
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

#ifndef LMMS_GUI_TILING_WINDOW_MANAGER_H
#define LMMS_GUI_TILING_WINDOW_MANAGER_H

#include <memory>
#include <vector>

#include <QObject>
#include <QPointer>
#include <QRect>
#include <QTimer>

class QMdiArea;
class QMdiSubWindow;
class QWidget;

namespace lmms::gui
{

//! Lays the workspace's subwindows out as tiles that fill the workspace and never overlap, like a tiling window
//! manager (i3, bspwm). The tiles form a binary split tree; a hidden or closed window hands its space back to its
//! neighbour.
//!
//! A newly shown window goes where its kind belongs: the song editor at the top of the main area, the other editors
//! and the mixer stacked below it, and instrument windows, effect dialogs and the like stacked in columns along the
//! right. Nothing is placed where it would have to overlap another window if there's any other way; when the
//! preferred spot is full, the window goes to the next best one that still fits.
//!
//! With the mouse: drag a window's title bar onto another window to swap the two, onto one of that window's edges to
//! put it beside the other there, or against an edge of the workspace to give it that whole side. The highlight shows
//! where the window will land (in red if it can't fit there). Drag the gap between two tiles to resize them.
//! Double-clicking the title bar still maximizes a window over the others.
//!
//! Windows with a fixed size (instrument windows, effect dialogs) can't be stretched, so they sit centred in their
//! tile and the tree gives the spare room to their neighbours where it can.
class TilingWindowManager : public QObject
{
	Q_OBJECT
public:
	explicit TilingWindowManager(QMdiArea* area);
	~TilingWindowManager() override;

	bool isEnabled() const { return m_enabled; }

public slots:
	void setEnabled(bool enabled);
	//! Picks up windows that were shown, hidden or closed and lays the tiles out again
	void retile();

signals:
	void enabledChanged(bool enabled);

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	struct Node;
	enum class DropZone { None, Swap, Left, Right, Top, Bottom };

	//! Where a dragged window would land: on or beside a tile, or along an edge of the whole workspace
	struct Drop
	{
		Node* target = nullptr;
		DropZone zone = DropZone::None;
		bool workspaceEdge = false;

		bool operator==(const Drop&) const = default;
	};

	bool subWindowEvent(QMdiSubWindow* win, QEvent* event);
	bool viewportEvent(QEvent* event);

	void scheduleRetile();
	std::vector<QMdiSubWindow*> tileableWindows() const;
	void reconcile();
	void watch(QMdiSubWindow* win);

	void place(std::unique_ptr<Node>& root, QMdiSubWindow* win) const;
	void repair(std::unique_ptr<Node>& root, const QMdiSubWindow* keep = nullptr) const;
	int overflow(const Node* root) const;
	Node* leafAt(QPoint pos) const;
	Node* dividerAt(QPoint pos) const;

	QRect workRect() const;
	void apply();
	static QRect windowRect(const Node* leaf);

	Drop dropAt(QPoint pos) const;
	std::unique_ptr<Node> dropped(const Drop& drop) const;
	static void moveTile(std::unique_ptr<Node>& root, Node* source, const Drop& drop);
	void updateDropPreview(QPoint pos);
	void showDropPreview(const QRect& rect, bool fits = true);
	void finishDrag(QPoint pos);

	QMdiArea* m_area;
	bool m_enabled = false;
	bool m_applying = false;
	//! Windows were added or changed size, so the tiles may no longer fit
	bool m_needsRepair = false;
	std::unique_ptr<Node> m_root;
	std::vector<QPointer<QMdiSubWindow>> m_watched;
	QTimer m_retileTimer;

	//! Title bar drag in progress
	QPointer<QMdiSubWindow> m_dragWindow;
	QPoint m_dragStart;
	bool m_dragging = false;
	Drop m_hoveredDrop;
	QWidget* m_dropPreview = nullptr;

	//! Divider drag in progress
	Node* m_resizeNode = nullptr;
};

} // namespace lmms::gui

#endif // LMMS_GUI_TILING_WINDOW_MANAGER_H
