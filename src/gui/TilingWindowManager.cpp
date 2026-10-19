/*
 * TilingWindowManager.cpp - arranges the workspace subwindows as tiles
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

#include "TilingWindowManager.h"

#include <algorithm>
#include <climits>
#include <cmath>

#include <QApplication>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionTitleBar>

#include "DeprecationHelper.h"
#include "SubWindow.h"

namespace lmms::gui
{

namespace
{

constexpr int Gap = 6; //!< between two tiles
constexpr int Margin = 6; //!< between the tiles and the edge of the workspace
constexpr int EdgeBand = 40; //!< how close to the edge of the workspace a drop takes that whole side
constexpr double MainShare = 0.7; //!< of the width the main area gets when the first panel column opens

//! What a window is for decides where it goes when it opens
enum class Role
{
	Arrangement, //!< the song editor: top of the main area
	Editor, //!< piano roll, automation and pattern editors, mixer: stacked below it
	Panel, //!< instrument and sample track windows, effect dialogs, everything else: columns on the right
};

Role roleOf(const QMdiSubWindow* win)
{
	const auto widget = win ? win->widget() : nullptr;
	if (!widget) { return Role::Panel; }
	if (widget->inherits("lmms::gui::SongEditorWindow")) { return Role::Arrangement; }
	if (widget->inherits("lmms::gui::Editor") || widget->inherits("lmms::gui::MixerView")) { return Role::Editor; }
	return Role::Panel;
}

bool isPanel(const QMdiSubWindow* win)
{
	return roleOf(win) == Role::Panel;
}

int along(const QSize& size, Qt::Orientation orientation)
{
	return orientation == Qt::Horizontal ? size.width() : size.height();
}

long long areaOf(const QRect& rect)
{
	return static_cast<long long>(rect.width()) * rect.height();
}

//! Two extents side by side with a gap between them, without overflowing QWIDGETSIZE_MAX
int besideEachOther(int a, int b)
{
	return static_cast<int>(std::min<long long>(static_cast<long long>(a) + b + Gap, QWIDGETSIZE_MAX));
}

QSize minimumSizeOf(const QMdiSubWindow* win)
{
	return win->minimumSize().expandedTo(win->minimumSizeHint());
}

int titleBarHeightOf(const QMdiSubWindow* win)
{
	if (const auto sub = qobject_cast<const SubWindow*>(win)) { return sub->titleBarHeight(); }

	QStyleOptionTitleBar so;
	so.titleBarState = Qt::WindowActive;
	so.titleBarFlags = Qt::Window;
	return win->style()->pixelMetric(QStyle::PM_TitleBarHeight, &so, win);
}

//! Highlights where a dragged window will land, in red if it won't fit there
class DropPreview : public QWidget
{
public:
	using QWidget::QWidget;

	void setFits(bool fits)
	{
		if (fits == m_fits) { return; }
		m_fits = fits;
		update();
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const auto color = m_fits ? palette().color(QPalette::Highlight) : QColor{220, 60, 60};
		auto fill = color;
		fill.setAlpha(60);
		p.setPen(QPen{color, 2});
		p.setBrush(fill);
		p.drawRoundedRect(QRectF{rect()}.adjusted(1, 1, -1, -1), 4, 4);
	}

private:
	bool m_fits = true;
};

} // namespace




//! A tile (leaf, holding a window) or a split of two tiles (inner node)
struct TilingWindowManager::Node
{
	Node* parent = nullptr;
	QPointer<QMdiSubWindow> window;
	std::unique_ptr<Node> first;
	std::unique_ptr<Node> second;
	//! Horizontal: first is left of second; vertical: first is above second
	Qt::Orientation orientation = Qt::Horizontal;
	//! Share of the space that goes to first, before minimum and maximum sizes have their say
	double ratio = 0.5;
	//! Space this node was given by the last layout
	QRect cell;

	bool isLeaf() const { return !first; }

	QSize minimumSize() const
	{
		if (isLeaf()) { return window ? minimumSizeOf(window) : QSize{}; }
		const auto a = first->minimumSize();
		const auto b = second->minimumSize();
		return orientation == Qt::Horizontal
			? QSize{besideEachOther(a.width(), b.width()), std::max(a.height(), b.height())}
			: QSize{std::max(a.width(), b.width()), besideEachOther(a.height(), b.height())};
	}

	QSize maximumSize() const
	{
		if (isLeaf()) { return window ? window->maximumSize() : QSize{QWIDGETSIZE_MAX, QWIDGETSIZE_MAX}; }
		const auto a = first->maximumSize();
		const auto b = second->maximumSize();
		return orientation == Qt::Horizontal
			? QSize{besideEachOther(a.width(), b.width()), std::max(a.height(), b.height())}
			: QSize{std::max(a.width(), b.width()), besideEachOther(a.height(), b.height())};
	}

	std::vector<Node*> leaves()
	{
		auto result = std::vector<Node*>{};
		collectLeaves(result);
		return result;
	}

	void collectLeaves(std::vector<Node*>& leaves)
	{
		if (isLeaf()) { leaves.push_back(this); return; }
		first->collectLeaves(leaves);
		second->collectLeaves(leaves);
	}

	Node* find(const QMdiSubWindow* win)
	{
		if (isLeaf()) { return window == win ? this : nullptr; }
		const auto found = first->find(win);
		return found ? found : second->find(win);
	}

	bool holdsOnly(bool panels) const
	{
		if (isLeaf()) { return isPanel(window) == panels; }
		return first->holdsOnly(panels) && second->holdsOnly(panels);
	}

	//! The biggest subtrees holding only panels (or only editors)
	void collectRegions(bool panels, std::vector<Node*>& regions)
	{
		if (holdsOnly(panels)) { regions.push_back(this); return; }
		if (isLeaf()) { return; }
		first->collectRegions(panels, regions);
		second->collectRegions(panels, regions);
	}

	//! The stacks that this node's side by side splits are made of, left to right
	void collectColumns(std::vector<Node*>& columns)
	{
		if (isLeaf() || orientation != Qt::Horizontal) { columns.push_back(this); return; }
		first->collectColumns(columns);
		second->collectColumns(columns);
	}

	QRect divider() const
	{
		return orientation == Qt::Horizontal
			? QRect{QPoint{first->cell.right() + 1, cell.top()}, QPoint{second->cell.left() - 1, cell.bottom()}}
			: QRect{QPoint{cell.left(), first->cell.bottom() + 1}, QPoint{cell.right(), second->cell.top() - 1}};
	}

	//! A deep copy, to try changes on
	std::unique_ptr<Node> clone() const
	{
		auto copy = std::make_unique<Node>();
		copy->window = window;
		copy->orientation = orientation;
		copy->ratio = ratio;
		copy->cell = cell;
		if (!isLeaf())
		{
			copy->first = first->clone();
			copy->second = second->clone();
			copy->first->parent = copy.get();
			copy->second->parent = copy.get();
		}
		return copy;
	}

	//! The way down from the root to this node, for finding the same node in a clone
	std::vector<bool> path() const
	{
		auto result = std::vector<bool>{};
		for (auto node = this; node->parent; node = node->parent)
		{
			result.push_back(node == node->parent->second.get());
		}
		std::reverse(result.begin(), result.end());
		return result;
	}

	Node* follow(const std::vector<bool>& path)
	{
		auto node = this;
		for (const bool second : path) { node = second ? node->second.get() : node->first.get(); }
		return node;
	}

	//! Moves what this node holds into one half of it and a new tile for `win` into the other half
	Node* split(QMdiSubWindow* win, Qt::Orientation splitOrientation, bool newFirst, double share)
	{
		auto existing = std::make_unique<Node>();
		existing->parent = this;
		existing->window = window;
		existing->orientation = orientation;
		existing->ratio = ratio;
		existing->first = std::move(first);
		existing->second = std::move(second);
		if (existing->first)
		{
			existing->first->parent = existing.get();
			existing->second->parent = existing.get();
		}

		auto added = std::make_unique<Node>();
		added->parent = this;
		added->window = win;
		const auto result = added.get();

		window = nullptr;
		orientation = splitOrientation;
		ratio = share;
		first = newFirst ? std::move(added) : std::move(existing);
		second = newFirst ? std::move(existing) : std::move(added);
		return result;
	}

	//! Removes a tile; its sibling takes the parent's place
	static void remove(std::unique_ptr<Node>& root, Node* leaf)
	{
		const auto parent = leaf->parent;
		if (!parent)
		{
			root.reset();
			return;
		}

		// Overwriting the parent's children frees both the leaf and the (by then emptied) sibling
		const auto sibling = std::move(leaf == parent->first.get() ? parent->second : parent->first);
		parent->window = sibling->window;
		parent->orientation = sibling->orientation;
		parent->ratio = sibling->ratio;
		parent->first = std::move(sibling->first);
		parent->second = std::move(sibling->second);
		if (parent->first)
		{
			parent->first->parent = parent;
			parent->second->parent = parent;
		}
	}

	//! How many tiles this run of splits in one direction shares its space between
	int unitsAlong(Qt::Orientation direction) const
	{
		if (isLeaf() || orientation != direction) { return 1; }
		return first->unitsAlong(direction) + second->unitsAlong(direction);
	}

	//! Gives every tile of this run of splits in one direction the same share
	void balance()
	{
		if (isLeaf()) { return; }
		const int a = first->unitsAlong(orientation);
		const int b = second->unitsAlong(orientation);
		ratio = static_cast<double>(a) / (a + b);
		if (!first->isLeaf() && first->orientation == orientation) { first->balance(); }
		if (!second->isLeaf() && second->orientation == orientation) { second->balance(); }
	}

	//! After a tile joined or left a stack of panels, shares the stack out evenly again. Editors keep the sizes
	//! they were dragged to.
	void evenOutPanels()
	{
		auto top = isLeaf() ? parent : this;
		if (!top) { return; }
		while (top->parent && top->parent->orientation == top->orientation) { top = top->parent; }
		if (top->holdsOnly(true)) { top->balance(); }
	}

	void layout(const QRect& space)
	{
		cell = space;
		if (isLeaf()) { return; }

		const int available = std::max(0, along(space.size(), orientation) - Gap);
		const int firstMin = along(first->minimumSize(), orientation);
		const int firstMax = along(first->maximumSize(), orientation);
		const int secondMin = along(second->minimumSize(), orientation);
		const int secondMax = along(second->maximumSize(), orientation);

		// The ratio decides, as long as both sides get at least their minimum and at most their maximum
		const int low = std::max(firstMin, available - secondMax);
		const int high = std::min(firstMax, available - secondMin);
		int firstExtent;
		if (low <= high)
		{
			firstExtent = std::clamp(static_cast<int>(std::lround(available * ratio)), low, high);
		}
		else if (firstMin + secondMin > available)
		{
			// Not enough room for both: shrink them alike
			firstExtent = static_cast<int>(static_cast<long long>(available) * firstMin / (firstMin + secondMin));
		}
		else if (orientation == Qt::Vertical)
		{
			// More room than both can use: stacks pack towards the top, leaving the rest below
			firstExtent = firstMax;
		}
		else
		{
			// More room than both can use: share out what's left over
			firstExtent = (firstMax + available - secondMax) / 2;
		}

		auto firstCell = space;
		auto secondCell = space;
		if (orientation == Qt::Horizontal)
		{
			firstCell.setWidth(firstExtent);
			secondCell.setLeft(space.left() + firstExtent + Gap);
		}
		else
		{
			firstCell.setHeight(firstExtent);
			secondCell.setTop(space.top() + firstExtent + Gap);
		}

		first->layout(firstCell);
		second->layout(secondCell);
	}
};




TilingWindowManager::TilingWindowManager(QMdiArea* area) :
	QObject(area),
	m_area(area)
{
	m_retileTimer.setSingleShot(true);
	m_retileTimer.setInterval(0);
	connect(&m_retileTimer, &QTimer::timeout, this, &TilingWindowManager::retile);

	m_area->viewport()->installEventFilter(this);
}




TilingWindowManager::~TilingWindowManager() = default;




void TilingWindowManager::setEnabled(bool enabled)
{
	if (enabled == m_enabled) { return; }
	m_enabled = enabled;

	auto viewport = m_area->viewport();
	// hovering a gap between tiles shows a resize cursor
	viewport->setMouseTracking(enabled);

	if (enabled)
	{
		retile();
	}
	else
	{
		// The windows stay where the tiles put them, they just float again from here on
		m_root.reset();
		m_resizeNode = nullptr;
		m_dragWindow = nullptr;
		m_dragging = false;
		m_hoveredDrop = {};
		showDropPreview({});
		viewport->unsetCursor();
	}

	emit enabledChanged(enabled);
}




void TilingWindowManager::retile()
{
	if (!m_enabled) { return; }
	m_retileTimer.stop();

	reconcile();
	if (!m_root) { return; }

	if (m_needsRepair)
	{
		m_needsRepair = false;
		repair(m_root);
	}

	m_root->layout(workRect());
	apply();
}




void TilingWindowManager::scheduleRetile()
{
	if (m_enabled) { m_retileTimer.start(); }
}




bool TilingWindowManager::eventFilter(QObject* watched, QEvent* event)
{
	if (!m_enabled) { return false; }
	if (watched == m_area->viewport()) { return viewportEvent(event); }
	if (auto win = qobject_cast<QMdiSubWindow*>(watched)) { return subWindowEvent(win, event); }
	return false;
}




bool TilingWindowManager::subWindowEvent(QMdiSubWindow* win, QEvent* event)
{
	switch (event->type())
	{
		case QEvent::Show:
		case QEvent::Hide:
		case QEvent::WindowStateChange:
			scheduleRetile();
			return false;

		case QEvent::Move:
			// Something other than us moved the window (a project being loaded, a window re-attached): put it
			// back in its tile
			if (!m_applying) { scheduleRetile(); }
			return false;

		case QEvent::Resize:
		case QEvent::LayoutRequest:
			// The window's content changed size (another instrument plugin, a new tab), which may no longer fit
			// where it is
			if (!m_applying)
			{
				m_needsRepair = true;
				scheduleRetile();
			}
			return false;

		default:
			break;
	}

	// A maximized window covers the tiles and behaves as it always has
	if (win->isMaximized()) { return false; }

	switch (event->type())
	{
		case QEvent::MouseButtonPress:
		{
			const auto me = static_cast<QMouseEvent*>(event);
			if (me->button() != Qt::LeftButton) { return false; }

			m_area->setActiveSubWindow(win);
			if (position(me).y() < titleBarHeightOf(win))
			{
				m_dragWindow = win;
				m_dragStart = win->mapTo(m_area->viewport(), position(me));
				m_dragging = false;
				m_hoveredDrop = {};
			}
			// Swallowed so Qt doesn't start moving the window or resizing it by its frame
			return true;
		}

		case QEvent::MouseMove:
		{
			// Without a drag this also keeps Qt from offering resize cursors on the frame
			if (!m_dragWindow) { return true; }

			const auto pos = win->mapTo(m_area->viewport(), position(static_cast<QMouseEvent*>(event)));
			if (!m_dragging && (pos - m_dragStart).manhattanLength() < QApplication::startDragDistance())
			{
				return true;
			}

			m_dragging = true;
			win->setCursor(Qt::SizeAllCursor);
			updateDropPreview(pos);
			return true;
		}

		case QEvent::MouseButtonRelease:
		{
			const auto me = static_cast<QMouseEvent*>(event);
			if (me->button() != Qt::LeftButton) { return false; }

			if (m_dragging) { finishDrag(win->mapTo(m_area->viewport(), position(me))); }
			m_dragWindow = nullptr;
			m_dragging = false;
			m_hoveredDrop = {};
			showDropPreview({});
			win->unsetCursor();
			return true;
		}

		case QEvent::MouseButtonDblClick:
		{
			const auto me = static_cast<QMouseEvent*>(event);
			if (me->button() != Qt::LeftButton || position(me).y() >= titleBarHeightOf(win)) { return true; }

			// Qt only maximizes on a double-click after it has seen the title bar hovered, which we keep
			// from it above, so do it here
			if (win->windowFlags() & Qt::WindowMaximizeButtonHint) { win->showMaximized(); }
			return true;
		}

		default:
			return false;
	}
}




bool TilingWindowManager::viewportEvent(QEvent* event)
{
	switch (event->type())
	{
		case QEvent::Resize:
			retile();
			return false;

		case QEvent::ChildAdded:
			// probably a new subwindow
			scheduleRetile();
			return false;

		case QEvent::MouseButtonPress:
		{
			const auto me = static_cast<QMouseEvent*>(event);
			if (me->button() == Qt::LeftButton) { m_resizeNode = dividerAt(position(me)); }
			// Swallowed either way: there's nothing off-screen to pan to
			return true;
		}

		case QEvent::MouseMove:
		{
			const auto pos = position(static_cast<QMouseEvent*>(event));
			if (!m_resizeNode)
			{
				const auto node = dividerAt(pos);
				if (!node) { m_area->viewport()->unsetCursor(); }
				else
				{
					m_area->viewport()->setCursor(
						node->orientation == Qt::Horizontal ? Qt::SplitHCursor : Qt::SplitVCursor);
				}
				return true;
			}

			const auto& cell = m_resizeNode->cell;
			const auto horizontal = m_resizeNode->orientation == Qt::Horizontal;
			const int available = along(cell.size(), m_resizeNode->orientation) - Gap;
			if (available <= 0) { return true; }

			const int offset = (horizontal ? pos.x() - cell.left() : pos.y() - cell.top()) - Gap / 2;
			m_resizeNode->ratio = std::clamp(static_cast<double>(offset) / available, 0.05, 0.95);
			m_root->layout(workRect());
			apply();
			return true;
		}

		case QEvent::MouseButtonRelease:
		case QEvent::MouseButtonDblClick:
			m_resizeNode = nullptr;
			return true;

		default:
			return false;
	}
}




std::vector<QMdiSubWindow*> TilingWindowManager::tileableWindows() const
{
	auto windows = std::vector<QMdiSubWindow*>{};
	for (const auto win : m_area->subWindowList())
	{
		// A detached window hides its frame in the workspace, so this leaves those out as well
		if (!win->isHidden() && !win->isMinimized()) { windows.push_back(win); }
	}
	return windows;
}




void TilingWindowManager::reconcile()
{
	for (const auto win : m_area->subWindowList()) { watch(win); }

	const auto windows = tileableWindows();

	// Hand the tiles of closed and hidden windows back to their neighbours. Removing a leaf frees its
	// sibling too, so look the leaves up again after each removal.
	while (m_root)
	{
		const auto leaves = m_root->leaves();
		const auto stale = std::find_if(leaves.begin(), leaves.end(), [&](const Node* leaf) {
			return !leaf->window || std::find(windows.begin(), windows.end(), leaf->window) == windows.end();
		});
		if (stale == leaves.end()) { break; }

		m_resizeNode = nullptr;
		const auto parent = (*stale)->parent;
		Node::remove(m_root, *stale);
		if (parent) { parent->evenOutPanels(); }
	}

	auto added = std::vector<QMdiSubWindow*>{};
	for (const auto win : windows)
	{
		if (!m_root || !m_root->find(win)) { added.push_back(win); }
	}
	if (added.empty()) { return; }

	// The song editor first, then the other editors, then the panels in the order they were opened, so each
	// finds the ones it goes next to already in place
	std::stable_sort(added.begin(), added.end(), [](const QMdiSubWindow* a, const QMdiSubWindow* b) {
		return roleOf(a) < roleOf(b);
	});

	m_resizeNode = nullptr;
	for (const auto win : added) { place(m_root, win); }
	m_needsRepair = true;
}




void TilingWindowManager::watch(QMdiSubWindow* win)
{
	m_watched.erase(std::remove_if(m_watched.begin(), m_watched.end(), [](const auto& w) { return w.isNull(); }),
		m_watched.end());
	if (std::find(m_watched.begin(), m_watched.end(), win) != m_watched.end()) { return; }

	win->installEventFilter(this);
	win->unsetCursor();
	m_watched.emplace_back(win);
}




void TilingWindowManager::place(std::unique_ptr<Node>& root, QMdiSubWindow* win) const
{
	if (!root)
	{
		root = std::make_unique<Node>();
		root->window = win;
		return;
	}

	//! Splitting a node to make room for the window
	struct Candidate
	{
		Node* node;
		Qt::Orientation orientation;
		bool newFirst;
		double ratio;
		bool balance;
	};
	auto candidates = std::vector<Candidate>{};

	root->layout(workRect());
	const auto role = roleOf(win);
	const bool panel = role == Role::Panel;

	// Where windows of this kind already are
	auto regions = std::vector<Node*>{};
	root->collectRegions(panel, regions);
	const auto region = regions.empty() ? nullptr : *std::max_element(regions.begin(), regions.end(),
		[](const Node* a, const Node* b) { return areaOf(a->cell) < areaOf(b->cell); });

	if (panel && region)
	{
		// Below the panels already open, filling one column after the other, and then in a new column
		auto columns = std::vector<Node*>{};
		region->collectColumns(columns);
		for (const auto column : columns) { candidates.push_back({column, Qt::Vertical, false, 0.5, true}); }
		candidates.push_back({region, Qt::Horizontal, false, 0.5, true});
	}
	else if (panel)
	{
		// The first panel starts a column along the right of the workspace
		candidates.push_back({root.get(), Qt::Horizontal, false, MainShare, false});
	}
	else if (region)
	{
		// The song editor on top of the other editors, the rest below them
		candidates.push_back({region, Qt::Vertical, role == Role::Arrangement, 0.5, true});
	}
	else
	{
		// The first editor takes the main area, left of the panels
		candidates.push_back({root.get(), Qt::Horizontal, true, MainShare, false});
	}

	// Failing that, wherever there's room: the biggest tiles first, split across their longer side first
	auto leaves = root->leaves();
	std::stable_sort(leaves.begin(), leaves.end(), [](const Node* a, const Node* b) {
		return areaOf(a->cell) > areaOf(b->cell);
	});
	for (const auto leaf : leaves)
	{
		const auto longer = leaf->cell.width() >= leaf->cell.height() ? Qt::Horizontal : Qt::Vertical;
		const auto shorter = longer == Qt::Horizontal ? Qt::Vertical : Qt::Horizontal;
		candidates.push_back({leaf, longer, false, 0.5, false});
		candidates.push_back({leaf, shorter, false, 0.5, false});
	}

	const auto split = [win](Node* node, const Candidate& candidate) {
		node->split(win, candidate.orientation, candidate.newFirst, candidate.ratio);
		if (candidate.balance) { node->balance(); }
	};

	// The first that fits, or else the one that overlaps least
	const Candidate* best = nullptr;
	int bestOverflow = INT_MAX;
	for (const auto& candidate : candidates)
	{
		auto trial = root->clone();
		split(trial->follow(candidate.node->path()), candidate);
		const int excess = overflow(trial.get());
		if (excess < bestOverflow)
		{
			best = &candidate;
			bestOverflow = excess;
			if (excess == 0) { break; }
		}
	}

	split(best->node, *best);
}




void TilingWindowManager::repair(std::unique_ptr<Node>& root, const QMdiSubWindow* keep) const
{
	// While the windows don't fit, move whichever one helps the most somewhere else. The overflow shrinks
	// every round, so this ends.
	int excess = overflow(root.get());
	while (excess > 0)
	{
		std::unique_ptr<Node> bestTree;
		for (const auto leaf : root->leaves())
		{
			const auto win = leaf->window.data();
			if (!win || win == keep || leaf == root.get()) { continue; }

			auto trial = root->clone();
			Node::remove(trial, trial->follow(leaf->path()));
			place(trial, win);

			const int trialExcess = overflow(trial.get());
			if (trialExcess < excess)
			{
				excess = trialExcess;
				bestTree = std::move(trial);
			}
		}

		if (!bestTree) { return; }
		root = std::move(bestTree);
	}
}




int TilingWindowManager::overflow(const Node* root) const
{
	if (!root) { return 0; }

	// Each split gives both sides at least their minimum when it can, so if the whole tree fits, every window
	// fits in its tile
	const auto needed = root->minimumSize();
	const auto room = workRect().size();
	return std::max(0, needed.width() - room.width()) + std::max(0, needed.height() - room.height());
}




TilingWindowManager::Node* TilingWindowManager::leafAt(QPoint pos) const
{
	if (!m_root) { return nullptr; }

	const auto leaves = m_root->leaves();
	const auto it = std::find_if(leaves.begin(), leaves.end(), [pos](const Node* leaf) {
		return leaf->cell.contains(pos);
	});
	return it != leaves.end() ? *it : nullptr;
}




TilingWindowManager::Node* TilingWindowManager::dividerAt(QPoint pos) const
{
	auto node = m_root.get();
	// The gaps of the children lie inside their cells, so walk down towards the point
	while (node && !node->isLeaf())
	{
		if (node->divider().contains(pos)) { return node; }
		node = node->first->cell.contains(pos) ? node->first.get()
			: node->second->cell.contains(pos) ? node->second.get()
			: nullptr;
	}
	return nullptr;
}




QRect TilingWindowManager::workRect() const
{
	return m_area->viewport()->rect().adjusted(Margin, Margin, -Margin, -Margin);
}




void TilingWindowManager::apply()
{
	if (!m_root) { return; }

	m_applying = true;
	for (const auto leaf : m_root->leaves())
	{
		const auto win = leaf->window.data();
		if (!win || win->isMaximized()) { continue; }

		const auto rect = windowRect(leaf);
		if (win->geometry() != rect) { win->setGeometry(rect); }
	}
	m_applying = false;
}




QRect TilingWindowManager::windowRect(const Node* leaf)
{
	const auto& cell = leaf->cell;
	const auto win = leaf->window.data();

	// A window that can't fill its tile sits at the top of it, centred across
	const auto size = cell.size().boundedTo(win->maximumSize()).expandedTo(minimumSizeOf(win));
	return {QPoint{cell.left() + std::max(0, (cell.width() - size.width()) / 2), cell.top()}, size};
}




TilingWindowManager::Drop TilingWindowManager::dropAt(QPoint pos) const
{
	// Nowhere to go with only one tile
	if (!m_root || m_root->isLeaf()) { return {}; }

	// Against an edge of the workspace: that whole side
	const auto area = m_area->viewport()->rect();
	const int left = pos.x() - area.left();
	const int right = area.right() - pos.x();
	const int top = pos.y() - area.top();
	const int bottom = area.bottom() - pos.y();
	const int nearestEdge = std::min({left, right, top, bottom});
	if (nearestEdge < EdgeBand)
	{
		const auto zone = nearestEdge == left ? DropZone::Left
			: nearestEdge == right ? DropZone::Right
			: nearestEdge == top ? DropZone::Top
			: DropZone::Bottom;
		return {m_root.get(), zone, true};
	}

	const auto target = leafAt(pos);
	if (!target || target->window == m_dragWindow) { return {}; }

	// The middle of a tile swaps with it, its edges put the window beside it
	const auto& cell = target->cell;
	const double x = static_cast<double>(pos.x() - cell.left()) / std::max(1, cell.width());
	const double y = static_cast<double>(pos.y() - cell.top()) / std::max(1, cell.height());

	if (x > 0.3 && x < 0.7 && y > 0.3 && y < 0.7) { return {target, DropZone::Swap, false}; }

	const double nearest = std::min({x, 1 - x, y, 1 - y});
	const auto zone = nearest == x ? DropZone::Left
		: nearest == 1 - x ? DropZone::Right
		: nearest == y ? DropZone::Top
		: DropZone::Bottom;
	return {target, zone, false};
}




std::unique_ptr<TilingWindowManager::Node> TilingWindowManager::dropped(const Drop& drop) const
{
	const auto source = m_root ? m_root->find(m_dragWindow) : nullptr;
	if (!source || drop.zone == DropZone::None) { return nullptr; }

	auto trial = m_root->clone();
	auto mapped = drop;
	mapped.target = trial->follow(drop.target->path());
	moveTile(trial, trial->follow(source->path()), mapped);

	// Make room for the window where it was dropped by moving the others out of its way if need be
	repair(trial, m_dragWindow);
	return trial;
}




void TilingWindowManager::moveTile(std::unique_ptr<Node>& root, Node* source, const Drop& drop)
{
	if (drop.zone == DropZone::Swap)
	{
		std::swap(source->window, drop.target->window);
		return;
	}

	// Removing the source can free the target's node (when they are siblings), so find it again after. A
	// workspace edge drop targets the root, which stays.
	const QPointer<QMdiSubWindow> targetWindow = drop.target->window;
	const auto win = source->window.data();
	const auto sourceParent = source->parent;
	Node::remove(root, source);
	if (sourceParent) { sourceParent->evenOutPanels(); }

	const auto target = drop.workspaceEdge ? root.get() : root->find(targetWindow);
	if (!target) { return; }

	const auto orientation = drop.zone == DropZone::Left || drop.zone == DropZone::Right
		? Qt::Horizontal
		: Qt::Vertical;
	target->split(win, orientation, drop.zone == DropZone::Left || drop.zone == DropZone::Top, 0.5);
	target->evenOutPanels();
}




void TilingWindowManager::updateDropPreview(QPoint pos)
{
	const auto drop = dropAt(pos);
	if (drop == m_hoveredDrop) { return; }
	m_hoveredDrop = drop;

	const auto trial = dropped(drop);
	if (!trial)
	{
		showDropPreview({});
		return;
	}

	trial->layout(workRect());
	const auto leaf = trial->find(m_dragWindow);
	showDropPreview(leaf ? windowRect(leaf) : QRect{}, overflow(trial.get()) <= overflow(m_root.get()));
}




void TilingWindowManager::showDropPreview(const QRect& rect, bool fits)
{
	if (rect.isNull())
	{
		if (m_dropPreview) { m_dropPreview->hide(); }
		return;
	}

	if (!m_dropPreview)
	{
		m_dropPreview = new DropPreview(m_area->viewport());
		m_dropPreview->setAttribute(Qt::WA_TransparentForMouseEvents);
	}
	static_cast<DropPreview*>(m_dropPreview)->setFits(fits);
	m_dropPreview->setGeometry(rect);
	m_dropPreview->show();
	m_dropPreview->raise();
}




void TilingWindowManager::finishDrag(QPoint pos)
{
	auto trial = dropped(dropAt(pos));

	// A drop that would leave windows overlapping more than they do now is refused (its preview was red)
	if (!trial || overflow(trial.get()) > overflow(m_root.get())) { return; }

	m_resizeNode = nullptr;
	m_root = std::move(trial);
	retile();
}




} // namespace lmms::gui
