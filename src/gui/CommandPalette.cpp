/*
 * CommandPalette.cpp - fuzzy-searchable launcher for every action in LMMS
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

#include "CommandPalette.h"

#include <QAction>
#include <QApplication>
#include <QGraphicsDropShadowEffect>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QAbstractButton>
#include <QRegularExpression>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

#include "AutomationEditor.h"
#include "ConfigManager.h"
#include "Effect.h"
#include "EffectChain.h"
#include "EffectRackView.h"
#include "embed.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "InstrumentTrack.h"
#include "InstrumentTrackView.h"
#include "MainWindow.h"
#include "Mixer.h"
#include "MixerChannelView.h"
#include "MixerView.h"
#include "ModernWidgets.h"
#include "PatternEditor.h"
#include "PianoRoll.h"
#include "PluginFactory.h"
#include "Song.h"
#include "SongEditor.h"
#include "TrackView.h"

namespace lmms::gui
{

namespace
{

constexpr int MaxRecent = 10;
constexpr int MaxShown = 200;
constexpr int PaletteWidth = 640;

enum Roles
{
	CategoryRole = Qt::UserRole + 1,
	ShortcutRole,
	PositionsRole,
	CommandIndexRole,
	RecentRole
};

bool isWordBoundary(const QString& text, int i)
{
	if (i == 0) { return true; }
	const QChar prev = text[i - 1];
	const QChar cur = text[i];
	if (!prev.isLetterOrNumber()) { return true; }
	if (prev.isLower() && cur.isUpper()) { return true; }
	if (prev.isLetter() != cur.isLetter()) { return true; }
	return false;
}

//! Score one query word against @p text, taking the better of a contiguous substring match and a
//! subsequence match that prefers word starts (so "se" finds "Song Editor" as well as "Pause")
int matchWord(const QString& word, const QString& text, QVector<int>* positions)
{
	if (word.isEmpty()) { return 0; }

	// Contiguous substring: try every occurrence and keep the best placed one
	int substringScore = -1;
	int substringAt = -1;
	for (int at = text.indexOf(word, 0, Qt::CaseInsensitive); at >= 0;
		at = text.indexOf(word, at + 1, Qt::CaseInsensitive))
	{
		int score = 50 + word.size() * 10;
		if (isWordBoundary(text, at)) { score += 40; }
		if (at == 0) { score += 20; }
		score -= std::min(at, 20);
		if (score > substringScore) { substringScore = score; substringAt = at; }
	}

	// Subsequence: greedily match, jumping to word starts unless the run is contiguous
	QVector<int> found;
	int subsequenceScore = 0;
	bool allBoundaries = true;
	int ti = 0;
	int prevMatch = -2;
	for (int wi = 0; wi < word.size() && subsequenceScore >= 0; ++wi)
	{
		const QChar wc = word[wi].toLower();
		int boundaryAt = -1;
		int anyAt = -1;
		for (int j = ti; j < text.size(); ++j)
		{
			if (text[j].toLower() != wc) { continue; }
			if (anyAt < 0) { anyAt = j; }
			if (isWordBoundary(text, j)) { boundaryAt = j; break; }
		}
		if (anyAt < 0) { subsequenceScore = -1; break; }
		const int at = (anyAt == prevMatch + 1 || boundaryAt < 0) ? anyAt : boundaryAt;
		const bool boundary = isWordBoundary(text, at);
		subsequenceScore += 8;
		if (at == prevMatch + 1) { subsequenceScore += 12; }
		if (boundary) { subsequenceScore += 25; }
		allBoundaries = allBoundaries && (boundary || at == prevMatch + 1);
		subsequenceScore -= std::min(at - ti, 8);
		found.push_back(at);
		prevMatch = at;
		ti = at + 1;
	}
	// Acronyms such as "pr" for "Piano Roll" are deliberate, reward them
	if (subsequenceScore >= 0 && allBoundaries && word.size() > 1) { subsequenceScore += 20; }

	if (substringScore < 0 && subsequenceScore < 0) { return -1; }
	if (substringScore >= subsequenceScore)
	{
		if (positions) { for (int i = 0; i < word.size(); ++i) { positions->push_back(substringAt + i); } }
		return substringScore;
	}
	if (positions) { *positions += found; }
	return subsequenceScore;
}

QString stripMnemonic(QString text)
{
	text.replace(QRegularExpression("&(?!&)"), QString());
	return text.replace("&&", "&");
}


class PaletteDelegate : public QStyledItemDelegate
{
public:
	using QStyledItemDelegate::QStyledItemDelegate;

	QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override
	{
		return QSize(PaletteWidth - 24, 36);
	}

	void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override
	{
		p->save();
		p->setRenderHint(QPainter::Antialiasing);
		const QRectF r = QRectF(option.rect).adjusted(4, 1, -4, -1);
		const bool selected = option.state & QStyle::State_Selected;

		if (selected)
		{
			QPainterPath path;
			path.addRoundedRect(r, 6, 6);
			p->fillPath(path, modern::activeButtonColor());
			p->fillRect(QRectF(r.left(), r.top() + 7, 3, r.height() - 14), modern::accentColor());
		}

		// Icon
		const auto icon = index.data(Qt::DecorationRole).value<QIcon>();
		const QRect iconRect(int(r.left()) + 12, int(r.center().y()) - 8, 16, 16);
		if (!icon.isNull()) { icon.paint(p, iconRect); }

		// Shortcut keycaps on the right
		qreal right = r.right() - 10;
		const QString shortcut = index.data(ShortcutRole).toString();
		QFont small = option.font;
		small.setPointSizeF(std::max(7.0, small.pointSizeF() - 1));
		p->setFont(small);
		const QFontMetricsF smallFm(small);
		if (!shortcut.isEmpty())
		{
			const QStringList keys = shortcut.split('+', Qt::SkipEmptyParts);
			for (int i = keys.size() - 1; i >= 0; --i)
			{
				const qreal w = std::max(18.0, smallFm.horizontalAdvance(keys[i]) + 10);
				const QRectF cap(right - w, r.center().y() - 9, w, 18);
				QPainterPath capPath;
				capPath.addRoundedRect(cap, 4, 4);
				p->fillPath(capPath, modern::sectionColor());
				p->setPen(QColor(255, 255, 255, 30));
				p->drawPath(capPath);
				p->setPen(modern::dimTextColor());
				p->drawText(cap, Qt::AlignCenter, keys[i]);
				right = cap.left() - 4;
			}
			right -= 8;
		}

		// Category pill
		const QString category = index.data(CategoryRole).toString();
		if (!category.isEmpty())
		{
			const qreal w = smallFm.horizontalAdvance(category) + 14;
			const QRectF pill(right - w, r.center().y() - 9, w, 18);
			QPainterPath pillPath;
			pillPath.addRoundedRect(pill, 9, 9);
			p->fillPath(pillPath, QColor(255, 255, 255, selected ? 22 : 12));
			p->setPen(modern::dimTextColor());
			p->drawText(pill, Qt::AlignCenter, category);
			right = pill.left() - 10;
		}

		// Title with the matched characters highlighted
		QFont titleFont = option.font;
		p->setFont(titleFont);
		QFont boldFont = titleFont;
		boldFont.setBold(true);
		const QFontMetricsF fm(titleFont);
		const QFontMetricsF boldFm(boldFont);
		const QString title = index.data(Qt::DisplayRole).toString();
		const auto positions = index.data(PositionsRole).value<QVector<int>>();
		qreal x = iconRect.right() + 12;
		const qreal baseline = r.center().y() + fm.ascent() / 2 - 1;
		const qreal maxX = right;
		for (int i = 0; i < title.size(); ++i)
		{
			const bool hit = positions.contains(i);
			const QFontMetricsF& m = hit ? boldFm : fm;
			const qreal adv = m.horizontalAdvance(title[i]);
			if (x + adv > maxX - fm.horizontalAdvance("…"))
			{
				p->setFont(titleFont);
				p->setPen(modern::dimTextColor());
				p->drawText(QPointF(x, baseline), "…");
				break;
			}
			p->setFont(hit ? boldFont : titleFont);
			p->setPen(hit ? modern::accentColor() : modern::textColor());
			p->drawText(QPointF(x, baseline), QString(title[i]));
			x += adv;
		}
		p->restore();
	}
};

} // namespace


int fuzzyMatchScore(const QString& query, const QString& text, QVector<int>* positions)
{
	const QStringList words = query.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
	if (words.isEmpty()) { return 0; }
	int total = 0;
	for (const auto& word : words)
	{
		const int score = matchWord(word, text, positions);
		if (score < 0) { return -1; }
		total += score;
	}
	// Shorter texts are more specific matches
	return total - std::min(static_cast<int>(text.size()) / 4, 20);
}




CommandPalette::CommandPalette(QWidget* parent) :
	QFrame(parent),
	m_search(new QLineEdit(this)),
	m_list(new QListWidget(this))
{
	setObjectName("commandPalette");
	setFixedWidth(PaletteWidth);
	setAttribute(Qt::WA_StyledBackground, false);
	hide();

	auto layout = new QVBoxLayout(this);
	layout->setContentsMargins(10, 10, 10, 10);
	layout->setSpacing(8);

	m_search->setPlaceholderText(tr("Search commands, instruments, effects, tracks…   try \"tempo 128\""));
	m_search->setClearButtonEnabled(true);
	m_search->setStyleSheet(QString(
		"QLineEdit { background: %1; color: %2; border: 1px solid %3; border-radius: 6px;"
		" padding: 8px 10px; font-size: 13px; selection-background-color: %4; }"
		"QLineEdit:focus { border: 1px solid %4; }")
		.arg(modern::displayColor().name(), modern::textColor().name(),
			modern::sectionBorder().name(), modern::accentColor().darker(150).name()));
	layout->addWidget(m_search);

	m_list->setItemDelegate(new PaletteDelegate(m_list));
	m_list->setFrameShape(QFrame::NoFrame);
	m_list->setFocusPolicy(Qt::NoFocus);
	m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_list->setUniformItemSizes(true);
	m_list->setStyleSheet("QListWidget { background: transparent; outline: none; }");
	m_list->setMouseTracking(true);
	layout->addWidget(m_list);

	auto shadow = new QGraphicsDropShadowEffect(this);
	shadow->setBlurRadius(40);
	shadow->setOffset(0, 10);
	shadow->setColor(QColor(0, 0, 0, 170));
	setGraphicsEffect(shadow);

	m_search->installEventFilter(this);
	connect(m_search, &QLineEdit::textChanged, this, &CommandPalette::refilter);
	connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) { runRow(m_list->row(item)); });

	m_recent = ConfigManager::inst()->value("commandpalette", "recent").split('\n', Qt::SkipEmptyParts);

	if (parent) { parent->installEventFilter(this); }
	qApp->installEventFilter(this);
}




void CommandPalette::popup()
{
	collectCommands();
	m_search->clear();
	refilter();
	reposition();
	show();
	raise();
	m_search->setFocus(Qt::PopupFocusReason);
}




void CommandPalette::setQuery(const QString& query)
{
	m_search->setText(query);
	refilter();
}




int CommandPalette::visibleCount() const
{
	return m_list->count();
}




QString CommandPalette::visibleTitle(int row) const
{
	const auto item = m_list->item(row);
	return item ? item->text() : QString();
}




void CommandPalette::runRow(int row)
{
	const auto item = m_list->item(row);
	if (!item) { return; }

	const int index = item->data(CommandIndexRole).toInt();
	const Command* command = nullptr;
	if (index >= 0 && index < static_cast<int>(m_commands.size())) { command = &m_commands[index]; }
	else if (index < 0 && -index - 1 < static_cast<int>(m_dynamicCommands.size()))
	{
		command = &m_dynamicCommands[-index - 1];
	}
	if (!command) { return; }

	auto run = command->run;
	rememberRecent(command->id);
	hide();
	// Let the palette hand back focus before running, so actions act on the right window
	QTimer::singleShot(0, qApp, [run] { if (run) { run(); } });
}




bool CommandPalette::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == parent() && event->type() == QEvent::Resize && isVisible())
	{
		reposition();
	}
	else if (watched == m_search && event->type() == QEvent::KeyPress)
	{
		const auto ke = static_cast<QKeyEvent*>(event);
		const int count = m_list->count();
		const int row = m_list->currentRow();
		switch (ke->key())
		{
		case Qt::Key_Escape:
			hide();
			return true;
		case Qt::Key_Return:
		case Qt::Key_Enter:
			runRow(std::max(row, 0));
			return true;
		case Qt::Key_Down:
			if (count) { m_list->setCurrentRow((row + 1) % count); }
			return true;
		case Qt::Key_Up:
			if (count) { m_list->setCurrentRow((row - 1 + count) % count); }
			return true;
		case Qt::Key_PageDown:
			if (count) { m_list->setCurrentRow(std::min(row + 10, count - 1)); }
			return true;
		case Qt::Key_PageUp:
			if (count) { m_list->setCurrentRow(std::max(row - 10, 0)); }
			return true;
		case Qt::Key_Tab:
			return true;
		default:
			break;
		}
	}
	else if (event->type() == QEvent::MouseButtonPress && isVisible())
	{
		// Clicking anywhere outside dismisses the palette, like any popup
		const auto w = qobject_cast<QWidget*>(watched);
		if (w && w != this && !isAncestorOf(w)) { hide(); }
	}
	return QFrame::eventFilter(watched, event);
}




void CommandPalette::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	QPainterPath path;
	path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);
	p.fillPath(path, modern::windowColor());
	p.setPen(QColor(255, 255, 255, 28));
	p.drawPath(path);
}




void CommandPalette::reposition()
{
	const auto w = parentWidget();
	if (!w) { return; }
	const int rows = std::clamp(m_list->count(), 1, 12);
	m_list->setFixedHeight(rows * 36 + 4);
	adjustSize();
	move((w->width() - width()) / 2, std::max(40, w->height() / 8));
}




void CommandPalette::rememberRecent(const QString& id)
{
	if (id.isEmpty()) { return; }
	m_recent.removeAll(id);
	m_recent.prepend(id);
	while (m_recent.size() > MaxRecent) { m_recent.removeLast(); }
	ConfigManager::inst()->setValue("commandpalette", "recent", m_recent.join('\n'));
}




void CommandPalette::addCommand(Command command)
{
	if (command.title.trimmed().isEmpty() || !command.run) { return; }
	if (command.id.isEmpty()) { command.id = command.category + "/" + command.title; }
	// Several toolbars expose the same action - keep the first
	if (m_ids.contains(command.id)) { return; }
	m_ids.insert(command.id);
	m_commands.push_back(std::move(command));
}




void CommandPalette::collectCommands()
{
	m_commands.clear();
	m_ids.clear();

	auto mainWindow = getGUI() ? getGUI()->mainWindow() : nullptr;
	auto song = Engine::getSong();

	// Transport and global toggles first so they rank well among equal matches
	if (song)
	{
		addCommand({"transport/play", tr("Play / pause song"), tr("Transport"), "Space",
			embed::getIconPixmap("play"), [] {
				if (auto se = getGUI()->songEditor()) { se->playAction()->trigger(); }
			}});
		addCommand({"transport/stop", tr("Stop playback"), tr("Transport"), QString(),
			embed::getIconPixmap("stop"), [] { Engine::getSong()->stop(); }});
		addCommand({"transport/metronome", tr("Toggle metronome"), tr("Transport"), QString(),
			embed::getIconPixmap("metronome"), [] {
				// Go through the toolbar button so its checked state stays in sync
				auto button = getGUI()->mainWindow()->findChild<QAbstractButton*>("metronomeToggle");
				if (button) { button->click(); }
			}});
	}

	if (mainWindow)
	{
		for (const auto action : mainWindow->menuBar()->actions())
		{
			if (action->menu()) { collectMenu(action->menu(), stripMnemonic(action->text())); }
		}
	}

	if (getGUI())
	{
		collectEditorActions(getGUI()->songEditor(), tr("Song Editor"), embed::getIconPixmap("songeditor"));
		collectEditorActions(getGUI()->pianoRoll(), tr("Piano Roll"), embed::getIconPixmap("piano"));
		collectEditorActions(getGUI()->patternEditor(), tr("Pattern Editor"), embed::getIconPixmap("pattern_track"));
		collectEditorActions(getGUI()->automationEditor(), tr("Automation Editor"),
			embed::getIconPixmap("automation"));
	}

	collectTracks();
	collectMixerChannels();

	// Arrangement markers
	if (song)
	{
		const auto& markers = song->getTimeline(Song::PlayMode::Song).markers();
		for (const auto& marker : markers)
		{
			const auto ticks = static_cast<int>(marker.pos);
			QPixmap swatch(16, 16);
			swatch.fill(Qt::transparent);
			{
				QPainter p(&swatch);
				p.setRenderHint(QPainter::Antialiasing);
				p.setPen(Qt::NoPen);
				p.setBrush(marker.color);
				p.drawRoundedRect(QRectF(2, 2, 12, 12), 3, 3);
			}
			addCommand({"marker/" + QString::number(ticks) + "/" + marker.name,
				tr("Go to marker: %1").arg(marker.name),
				tr("Marker at bar %1").arg(ticks / TimePos::ticksPerBar() + 1), QString(), QIcon(swatch),
				[ticks] {
					auto song = Engine::getSong();
					song->getTimeline(Song::PlayMode::Song).setTicks(ticks);
					if (!song->isPlaying()) { song->getTimeline(Song::PlayMode::None).setTicks(ticks); }
					if (auto se = getGUI()->songEditor())
					{
						if (auto sub = se->parentWidget()) { sub->show(); sub->raise(); }
						se->m_editor->update();
					}
				}});
		}
	}

	// The installed plugins don't change while LMMS runs, and listing them can be slow
	// (LADSPA, VST3), so build those commands only once
	if (m_pluginCommands.empty())
	{
		collectInstruments();
		collectEffects();
	}
	for (const auto& command : m_pluginCommands) { addCommand(command); }
}




void CommandPalette::collectMenu(QMenu* menu, const QString& path)
{
	// Many menus fill themselves lazily right before they are shown
	emit menu->aboutToShow();

	for (const auto action : menu->actions())
	{
		if (action->isSeparator() || !action->isVisible()) { continue; }
		QString text = stripMnemonic(action->text());
		QString shortcut = action->shortcut().toString(QKeySequence::NativeText);
		if (const int tab = text.indexOf('\t'); tab >= 0)
		{
			if (shortcut.isEmpty()) { shortcut = text.mid(tab + 1).trimmed(); }
			text = text.left(tab).trimmed();
		}
		if (action->menu())
		{
			collectMenu(action->menu(), path + " › " + text);
			continue;
		}
		QPointer<QAction> guarded = action;
		QIcon icon = action->icon();
		addCommand({"menu/" + path + "/" + text, text, path, shortcut, icon,
			[guarded] { if (guarded && guarded->isEnabled()) { guarded->trigger(); } }});
	}
}




void CommandPalette::collectEditorActions(QWidget* editor, const QString& category, const QIcon& fallbackIcon)
{
	if (!editor) { return; }
	static const QRegularExpression shortcutSuffix(R"(\s*\(([^()]*[+][^()]*|Space|Del|Delete)\)\s*$)");

	for (const auto action : editor->findChildren<QAction*>())
	{
		if (action->isSeparator() || action->menu()) { continue; }
		QString text = stripMnemonic(action->text()).trimmed();
		if (text.isEmpty()) { continue; }
		QString shortcut = action->shortcut().toString(QKeySequence::NativeText);
		const auto match = shortcutSuffix.match(text);
		if (match.hasMatch())
		{
			if (shortcut.isEmpty()) { shortcut = match.captured(1); }
			text = text.left(match.capturedStart()).trimmed();
		}
		QPointer<QAction> guarded = action;
		QPointer<QWidget> window = editor;
		addCommand({"editor/" + category + "/" + text, text, category, shortcut,
			action->icon().isNull() ? fallbackIcon : action->icon(),
			[guarded, window] {
				if (!guarded || !window) { return; }
				// Bring the editor up first, so the action has something to work on
				if (auto sub = window->parentWidget()) { sub->show(); sub->raise(); }
				window->show();
				window->setFocus();
				if (guarded->isEnabled()) { guarded->trigger(); }
			}});
	}
}




void CommandPalette::collectInstruments()
{
	auto descriptors = getPluginFactory()->descriptors(Plugin::Type::Instrument);
	for (const auto desc : descriptors)
	{
		const QIcon logo = desc->logo ? QIcon(desc->logo->pixmap()) : QIcon();
		Plugin::Descriptor::SubPluginFeatures::KeyList keys;
		if (desc->subPluginFeatures) { desc->subPluginFeatures->listSubPluginKeys(desc, keys); }
		else { keys.push_back(Plugin::Descriptor::SubPluginFeatures::Key(desc, desc->name)); }

		for (const auto& key : keys)
		{
			const QString name = desc->subPluginFeatures ? key.displayName() : QString(desc->displayName);
			const QString category = desc->subPluginFeatures
				? tr("Instrument · %1").arg(desc->displayName) : tr("Instrument");
			m_pluginCommands.push_back({"instrument/" + QString(desc->name) + "/" + key.name,
				tr("Add instrument: %1").arg(name), category, QString(), logo,
				[key] {
					auto track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
					if (!track) { return; }
					track->loadInstrument(key.desc->name, &key);
					// Open the new instrument so the user can start playing right away
					if (getGUI() && getGUI()->songEditor())
					{
						for (auto tv : getGUI()->songEditor()->m_editor->trackViews())
						{
							if (tv->getTrack() != track) { continue; }
							if (auto itv = dynamic_cast<InstrumentTrackView*>(tv)) { itv->toggleInstrumentWindow(true); }
						}
					}
				}});
		}
	}
}




void CommandPalette::collectEffects()
{
	auto mixerView = getGUI() ? getGUI()->mixerView() : nullptr;
	if (!mixerView) { return; }

	for (const auto desc : getPluginFactory()->descriptors(Plugin::Type::Effect))
	{
		const QIcon logo = desc->logo ? QIcon(desc->logo->pixmap()) : QIcon();
		Plugin::Descriptor::SubPluginFeatures::KeyList keys;
		if (desc->subPluginFeatures) { desc->subPluginFeatures->listSubPluginKeys(desc, keys); }
		else { keys.push_back(Plugin::Descriptor::SubPluginFeatures::Key(desc, desc->name)); }

		for (const auto& key : keys)
		{
			const QString name = desc->subPluginFeatures ? key.displayName() : QString(desc->displayName);
			const QString category = desc->subPluginFeatures
				? tr("Effect · %1").arg(desc->displayName) : tr("Effect");
			m_pluginCommands.push_back({"effect/" + QString(desc->name) + "/" + key.name,
				tr("Add effect to current mixer channel: %1").arg(name), category, QString(), logo,
				[key] {
					auto mixerView = getGUI()->mixerView();
					auto channel = mixerView->currentMixerChannel();
					if (!channel) { return; }
					// Show the mixer so the user sees where the effect went
					if (auto sub = mixerView->parentWidget()) { sub->show(); sub->raise(); }
					mixerView->show();
					channel->effectRackView()->addEffectFromKey(key);
				}});
		}
	}
}




void CommandPalette::collectTracks()
{
	if (!getGUI() || !getGUI()->songEditor()) { return; }
	for (auto tv : getGUI()->songEditor()->m_editor->trackViews())
	{
		Track* track = tv->getTrack();
		if (!track) { continue; }
		QPointer<TrackView> view = tv;
		const bool isInstrument = track->type() == Track::Type::Instrument;
		addCommand({"track/" + QString::number(reinterpret_cast<quintptr>(track)),
			tr("Go to track: %1").arg(track->name()),
			isInstrument ? tr("Instrument track") : tr("Track"), QString(),
			isInstrument ? embed::getIconPixmap("instrument_track") : QIcon(),
			[view] {
				if (!view) { return; }
				auto songEditor = getGUI()->songEditor();
				if (auto sub = songEditor->parentWidget()) { sub->show(); sub->raise(); }
				songEditor->m_editor->scrollToTrackView(view);
				if (auto itv = dynamic_cast<InstrumentTrackView*>(view.data())) { itv->toggleInstrumentWindow(true); }
			}});
	}
}




void CommandPalette::collectMixerChannels()
{
	auto mixer = Engine::mixer();
	if (!mixer || !getGUI() || !getGUI()->mixerView()) { return; }
	for (int i = 0; i < mixer->numChannels(); ++i)
	{
		const auto channel = mixer->mixerChannel(i);
		const QString name = i == 0 ? tr("Master") : channel->m_name;
		addCommand({"mixer/" + QString::number(i), tr("Go to mixer channel %1: %2").arg(i).arg(name),
			tr("Mixer"), QString(), embed::getIconPixmap("mixer"),
			[i] {
				auto mixerView = getGUI()->mixerView();
				if (i >= Engine::mixer()->numChannels()) { return; }
				if (auto sub = mixerView->parentWidget()) { sub->show(); sub->raise(); }
				mixerView->show();
				mixerView->setCurrentMixerChannel(i);
			}});
	}
}




void CommandPalette::refilter()
{
	const QString query = m_search->text().trimmed();
	m_list->clear();
	m_dynamicCommands.clear();

	// "tempo 128", "128 bpm", "bpm 90" -> instant tempo change
	static const QRegularExpression tempoQuery(R"(^(?:(?:tempo|bpm)\s*(\d{2,3})|(\d{2,3})\s*bpm)$)",
		QRegularExpression::CaseInsensitiveOption);
	const auto tempoMatch = tempoQuery.match(query);
	if (tempoMatch.hasMatch() && Engine::getSong())
	{
		const int bpm = (tempoMatch.captured(1).isEmpty() ? tempoMatch.captured(2) : tempoMatch.captured(1)).toInt();
		auto& model = Engine::getSong()->tempoModel();
		if (bpm >= model.minValue() && bpm <= model.maxValue())
		{
			m_dynamicCommands.push_back({QString(), tr("Set tempo to %1 BPM").arg(bpm), tr("Song"), QString(),
				embed::getIconPixmap("metronome"), [bpm] { Engine::getSong()->tempoModel().setValue(bpm); }});
		}
	}

	struct Hit { int index; int score; QVector<int> positions; bool recent; };
	std::vector<Hit> hits;
	for (int i = 0; i < static_cast<int>(m_commands.size()); ++i)
	{
		const auto& command = m_commands[i];
		const int recentRank = m_recent.indexOf(command.id);
		if (query.isEmpty())
		{
			if (recentRank >= 0) { hits.push_back({i, 1000 - recentRank, {}, true}); }
			continue;
		}
		// Search the title and the category together so "piano quantize" works, but only
		// highlight what landed in the title
		QVector<int> positions;
		int score = fuzzyMatchScore(query, command.title, &positions);
		if (score < 0)
		{
			positions.clear();
			score = fuzzyMatchScore(query, command.title + "  " + command.category, &positions);
			if (score < 0) { continue; }
			score -= 60;
		}
		if (recentRank >= 0) { score += 40 - recentRank * 3; }
		positions.erase(std::remove_if(positions.begin(), positions.end(),
			[&](int p) { return p >= command.title.size(); }), positions.end());
		hits.push_back({i, score, positions, recentRank >= 0});
	}

	if (query.isEmpty())
	{
		// No query: recent commands, then a curated set of everyday commands
		std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.score > b.score; });
		for (int i = 0; i < static_cast<int>(m_commands.size()); ++i)
		{
			const auto& id = m_commands[i].id;
			if (m_recent.contains(id)) { continue; }
			if (id.startsWith("transport/") || id.startsWith("menu/") || id.startsWith("track/"))
			{
				hits.push_back({i, 0, {}, false});
			}
		}
	}
	else
	{
		std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.score > b.score; });
	}

	for (int i = 0; i < static_cast<int>(m_dynamicCommands.size()); ++i)
	{
		const auto& command = m_dynamicCommands[i];
		auto item = new QListWidgetItem(command.icon, command.title, m_list);
		item->setData(CategoryRole, command.category);
		item->setData(CommandIndexRole, -i - 1);
	}

	int shown = 0;
	for (const auto& hit : hits)
	{
		if (++shown > MaxShown) { break; }
		const auto& command = m_commands[hit.index];
		auto item = new QListWidgetItem(command.icon, command.title, m_list);
		item->setData(CategoryRole, hit.recent && query.isEmpty() ? tr("Recent") : command.category);
		item->setData(ShortcutRole, command.shortcut);
		item->setData(PositionsRole, QVariant::fromValue(hit.positions));
		item->setData(CommandIndexRole, hit.index);
		item->setData(RecentRole, hit.recent);
	}

	if (m_list->count() > 0) { m_list->setCurrentRow(0); }
	if (isVisible()) { reposition(); }
}




CommandPaletteButton::CommandPaletteButton(QWidget* parent) :
	QWidget(parent)
{
	setCursor(Qt::PointingHandCursor);
	setFixedSize(sizeHint());
	setToolTip(tr("Command palette - search every action, instrument, effect and track (Ctrl+K)"));
}




void CommandPaletteButton::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	const QRectF r = QRectF(rect()).adjusted(1.5, 2.5, -1.5, -2.5);
	QPainterPath path;
	path.addRoundedRect(r, r.height() / 2, r.height() / 2);
	p.fillPath(path, m_hover ? modern::sectionColor().lighter(130) : modern::displayColor());
	p.setPen(m_hover ? modern::accentColor().darker(140) : QColor(255, 255, 255, 30));
	p.drawPath(path);

	// Magnifier glyph
	const QPointF c(r.left() + 15, r.center().y() - 1);
	p.setPen(QPen(modern::dimTextColor(), 1.6));
	p.drawEllipse(c, 4.5, 4.5);
	p.drawLine(c + QPointF(3.4, 3.4), c + QPointF(6.8, 6.8));

	QFont f = font();
	f.setPointSizeF(std::max(7.0, f.pointSizeF() - 1.5));
	p.setFont(f);
	const QFontMetricsF fm(f);

	// Keycap hint
	const QString hint = "Ctrl K";
	const qreal w = fm.horizontalAdvance(hint) + 10;
	const QRectF cap(r.right() - w - 8, r.center().y() - 8, w, 16);

	const QRectF textRect(r.left() + 28, r.top(), cap.left() - r.left() - 34, r.height());
	p.setPen(modern::dimTextColor());
	p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
		fm.elidedText(tr("Search…"), Qt::ElideRight, textRect.width()));

	QPainterPath capPath;
	capPath.addRoundedRect(cap, 4, 4);
	p.fillPath(capPath, modern::buttonColor());
	p.setPen(modern::textColor());
	p.drawText(cap, Qt::AlignCenter, hint);
}




void CommandPaletteButton::mousePressEvent(QMouseEvent* me)
{
	if (me->button() == Qt::LeftButton) { emit clicked(); }
}




bool CommandPaletteButton::event(QEvent* e)
{
	if (e->type() == QEvent::Enter) { m_hover = true; update(); }
	else if (e->type() == QEvent::Leave) { m_hover = false; update(); }
	return QWidget::event(e);
}

} // namespace lmms::gui
