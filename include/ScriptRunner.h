/*
 * ScriptRunner.h - runs Python scripts that control LMMS through the script server
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

#ifndef LMMS_SCRIPT_RUNNER_H
#define LMMS_SCRIPT_RUNNER_H

#include <QObject>
#include <QProcess>

#include "lmms_export.h"

namespace lmms::scripting
{

/**
	Runs Python with the bundled "lmms" module on its path and the connection details of the
	(auto-started) ScriptServer in its environment.

	Either runs a script file to completion (runFile) or keeps an interactive interpreter alive
	whose namespace persists between execute() calls (startConsole), like Blender's Python console.
*/
class LMMS_EXPORT ScriptRunner : public QObject
{
	Q_OBJECT
public:
	explicit ScriptRunner(QObject* parent = nullptr);
	~ScriptRunner() override;

	//! Python interpreter: $LMMS_PYTHON, the "scripting/python" setting, or python3/python from PATH
	static QString pythonExecutable();
	//! Directory that contains the "lmms" Python package
	static QString pythonModuleDir();
	//! Directory with the bundled example scripts
	static QString examplesDir();

	bool runFile(const QString& file, const QStringList& args = {});
	bool startConsole();
	//! Run @p code in the console interpreter (starting it if needed)
	void execute(const QString& code);
	void stop();

	bool isRunning() const;
	bool isConsole() const { return m_console; }
	bool isBusy() const { return m_busy; }

signals:
	void output(const QString& text, bool isError);
	void started();
	//! The process ended (script finished, console stopped or crashed)
	void finished(int exitCode);
	//! A block sent with execute() finished
	void executionFinished(bool ok);

private:
	bool launch(const QStringList& args, bool console);
	void readStdout();
	void readStderr();

	QProcess* m_process = nullptr;
	bool m_console = false;
	bool m_busy = false;
	QByteArray m_stdoutBuffer;
	QByteArray m_stderrBuffer; //!< an incomplete UTF-8 sequence from the last read
};

} // namespace lmms::scripting

#endif // LMMS_SCRIPT_RUNNER_H
