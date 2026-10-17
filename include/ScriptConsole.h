/*
 * ScriptConsole.h - Python scripting window (editor, output and interactive prompt)
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

#ifndef LMMS_GUI_SCRIPT_CONSOLE_H
#define LMMS_GUI_SCRIPT_CONSOLE_H

#include <QStringList>
#include <QWidget>

#include "lmms_export.h"

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QToolButton;

namespace lmms::scripting
{
class ScriptRunner;
}

namespace lmms::gui
{

/**
	A Blender-style Python console for LMMS: write a script in the editor and run it (Ctrl+Enter),
	or type single lines at the prompt. Code runs in a persistent Python interpreter with the
	"lmms" module imported, so variables survive between runs.
*/
class LMMS_EXPORT ScriptConsole : public QWidget
{
	Q_OBJECT
public:
	explicit ScriptConsole(QWidget* parent = nullptr);
	~ScriptConsole() override;

	//! Mirror the output of a script started elsewhere (e.g. lmms --script)
	void attachRunner(scripting::ScriptRunner* runner);

	void setScript(const QString& code);
	QString script() const;
	bool openFile(const QString& path);

	//! Run code in the console interpreter, echoing it into the output
	void runCode(const QString& code, bool echo);

	enum class Style { Normal, Error, Echo, Info };
	void appendOutput(const QString& text, Style style);

public slots:
	void run();
	void stop();
	void restart();
	void clearOutput();

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void createRunner();
	void openScript();
	bool saveScript(bool askForName);
	void updateState();
	void updateTitle();
	void showExamples();
	void copyConnectionSnippet();

	scripting::ScriptRunner* m_runner;
	QPlainTextEdit* m_editor;
	QPlainTextEdit* m_output;
	QLineEdit* m_prompt;
	QLabel* m_status;
	QToolButton* m_runButton;
	QToolButton* m_stopButton;
	QString m_fileName;
	QStringList m_history;
	int m_historyPos = 0;
	int m_externalScripts = 0;
};

} // namespace lmms::gui

#endif // LMMS_GUI_SCRIPT_CONSOLE_H
