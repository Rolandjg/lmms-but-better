/*
 * CommandPalette.h - fuzzy-searchable launcher for every action in LMMS
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

#ifndef LMMS_GUI_COMMAND_PALETTE_H
#define LMMS_GUI_COMMAND_PALETTE_H

#include <QFrame>
#include <QIcon>
#include <QSet>
#include <QVector>
#include <functional>
#include <vector>

#include "lmms_export.h"

class QLineEdit;
class QListWidget;
class QMenu;

namespace lmms::gui
{

//! Score how well @p query fuzzy-matches @p text. Every whitespace separated word of the query
//! has to match (in any order) as a case-insensitive subsequence. Returns -1 when there is no
//! match; higher is better. Matched character indices are appended to @p positions if given.
LMMS_EXPORT int fuzzyMatchScore(const QString& query, const QString& text, QVector<int>* positions = nullptr);


//! A searchable overlay listing everything the user can do: menu entries, editor tool buttons,
//! instruments, effects, tracks and mixer channels. Opened with Ctrl+K or Ctrl+Shift+P.
class LMMS_EXPORT CommandPalette : public QFrame
{
	Q_OBJECT
public:
	struct Command
	{
		QString id;        //!< stable key used to remember recently run commands
		QString title;
		QString category;
		QString shortcut;
		QIcon icon;
		std::function<void()> run;
	};

	explicit CommandPalette(QWidget* parent);

	//! Show the palette centered over the parent and gather a fresh command list
	void popup();

	//! Exposed for tests
	const std::vector<Command>& commands() const { return m_commands; }
	void setQuery(const QString& query);
	int visibleCount() const;
	QString visibleTitle(int row) const;
	void runRow(int row);

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
	void paintEvent(QPaintEvent*) override;

private:
	void collectCommands();
	void collectMenu(QMenu* menu, const QString& path);
	void collectEditorActions(QWidget* editor, const QString& category, const QIcon& fallbackIcon);
	void collectInstruments();
	void collectEffects();
	void collectTracks();
	void collectMixerChannels();
	void addCommand(Command command);
	void refilter();
	void reposition();
	void rememberRecent(const QString& id);

	QLineEdit* m_search;
	QListWidget* m_list;
	std::vector<Command> m_commands;
	std::vector<Command> m_pluginCommands; //!< instruments and effects, built once
	QSet<QString> m_ids;
	std::vector<Command> m_dynamicCommands; //!< derived from the query, e.g. "tempo 128"
	QStringList m_recent;
};


//! Search-field shaped button for the main toolbar that opens the command palette
class LMMS_EXPORT CommandPaletteButton : public QWidget
{
	Q_OBJECT
public:
	explicit CommandPaletteButton(QWidget* parent);
	QSize sizeHint() const override { return QSize(210, 30); }

signals:
	void clicked();

protected:
	void paintEvent(QPaintEvent*) override;
	void mousePressEvent(QMouseEvent*) override;
	bool event(QEvent* e) override;

private:
	bool m_hover = false;
};

} // namespace lmms::gui

#endif // LMMS_GUI_COMMAND_PALETTE_H
