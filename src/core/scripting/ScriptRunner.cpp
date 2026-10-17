/*
 * ScriptRunner.cpp - runs Python scripts that control LMMS through the script server
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

#include "ScriptRunner.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include "ConfigManager.h"
#include "ScriptServer.h"

namespace lmms::scripting
{

namespace
{

//! Printed by the console interpreter (lmms/console.py) after each executed block
constexpr char DoneMarker[] = "\x1eLMMS_DONE:";

QString findDataDir(const QString& relative, const QString& probe)
{
	if (const QByteArray env = qgetenv("LMMS_SCRIPTING_DIR"); !env.isEmpty())
	{
		const QDir dir(QDir(QString::fromLocal8Bit(env)).filePath(relative));
		if (dir.exists(probe)) { return dir.absolutePath(); }
	}
	for (const auto& base : QDir::searchPaths("data"))
	{
		const QDir dir(QDir(base).filePath("scripting/" + relative));
		if (dir.exists(probe)) { return dir.absolutePath(); }
	}
	return {};
}

//! Length of the part of @p data that doesn't end in the middle of a UTF-8 sequence
int completeUtf8Length(const QByteArray& data)
{
	int start = data.size() - 1;
	while (start >= 0 && data.size() - start < 4 && (static_cast<uchar>(data[start]) & 0xc0) == 0x80) { --start; }
	if (start < 0) { return data.size(); }
	const auto lead = static_cast<uchar>(data[start]);
	const int needed = lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : lead >= 0xc0 ? 2 : 1;
	return data.size() - start < needed ? start : data.size();
}

} // namespace




ScriptRunner::ScriptRunner(QObject* parent) :
	QObject(parent)
{
}




ScriptRunner::~ScriptRunner()
{
	if (m_process)
	{
		m_process->disconnect(this);
		m_process->kill();
		m_process->waitForFinished(1000);
	}
}




QString ScriptRunner::pythonExecutable()
{
	if (const QByteArray env = qgetenv("LMMS_PYTHON"); !env.isEmpty()) { return QString::fromLocal8Bit(env); }
	const QString configured = ConfigManager::inst()->value("scripting", "python");
	if (!configured.isEmpty()) { return configured; }
	for (const auto& name : {"python3", "python"})
	{
		const QString found = QStandardPaths::findExecutable(name);
		if (!found.isEmpty()) { return found; }
	}
	return "python3";
}




QString ScriptRunner::pythonModuleDir()
{
	return findDataDir("python", "lmms/__init__.py");
}




QString ScriptRunner::examplesDir()
{
	return findDataDir("examples", ".");
}




bool ScriptRunner::runFile(const QString& file, const QStringList& args)
{
	if (!QFileInfo(file).isFile())
	{
		emit output(tr("Script not found: %1").arg(file) + "\n", true);
		return false;
	}
	return launch(QStringList{"-u", QFileInfo(file).absoluteFilePath()} + args, false);
}




bool ScriptRunner::startConsole()
{
	if (isRunning() && m_console) { return true; }
	return launch({"-u", "-m", "lmms.console"}, true);
}




void ScriptRunner::execute(const QString& code)
{
	if (!(isRunning() && m_console) && !startConsole()) { return; }
	m_busy = true;
	const QJsonObject request{{"code", code}};
	m_process->write(QJsonDocument(request).toJson(QJsonDocument::Compact) + "\n");
}




void ScriptRunner::stop()
{
	if (!m_process) { return; }
	m_process->kill();
}




bool ScriptRunner::isRunning() const
{
	return m_process && m_process->state() != QProcess::NotRunning;
}




bool ScriptRunner::launch(const QStringList& args, bool console)
{
	if (isRunning())
	{
		emit output(tr("A script is already running.") + "\n", true);
		return false;
	}

	auto server = ScriptServer::instance();
	if (!server->start())
	{
		emit output(tr("Could not start the script server.") + "\n", true);
		return false;
	}

	const QString moduleDir = pythonModuleDir();
	if (moduleDir.isEmpty())
	{
		emit output(tr("Could not find the lmms Python module (data/scripting/python). "
			"Set LMMS_SCRIPTING_DIR to the scripting data directory.") + "\n", true);
		return false;
	}

	if (m_process) { m_process->deleteLater(); }
	m_process = new QProcess(this);
	m_console = console;
	m_busy = false;
	m_stdoutBuffer.clear();
	m_stderrBuffer.clear();

	auto env = QProcessEnvironment::systemEnvironment();
	server->addToEnvironment(env);
	const QString pythonPath = env.value("PYTHONPATH");
	env.insert("PYTHONPATH", pythonPath.isEmpty() ? moduleDir : moduleDir + QDir::listSeparator() + pythonPath);
	env.insert("PYTHONUNBUFFERED", "1");
	env.insert("PYTHONIOENCODING", "utf-8");
	m_process->setProcessEnvironment(env);

	connect(m_process, &QProcess::readyReadStandardOutput, this, &ScriptRunner::readStdout);
	connect(m_process, &QProcess::readyReadStandardError, this, &ScriptRunner::readStderr);
	connect(m_process, &QProcess::started, this, &ScriptRunner::started);
	connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
		if (error == QProcess::FailedToStart)
		{
			emit output(tr("Could not start Python (%1). Install Python 3 or set LMMS_PYTHON.")
				.arg(pythonExecutable()) + "\n", true);
			m_busy = false;
			emit finished(-1);
		}
	});
	connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
		[this](int exitCode, QProcess::ExitStatus status) {
			readStdout();
			readStderr();
			if (!m_stdoutBuffer.isEmpty()) { emit output(QString::fromUtf8(m_stdoutBuffer), false); }
			if (!m_stderrBuffer.isEmpty()) { emit output(QString::fromUtf8(m_stderrBuffer), true); }
			m_stdoutBuffer.clear();
			m_stderrBuffer.clear();
			const bool wasBusy = m_busy;
			m_busy = false;
			if (wasBusy) { emit executionFinished(false); }
			emit finished(status == QProcess::NormalExit ? exitCode : -1);
		});

	m_process->start(pythonExecutable(), args);
	return true;
}




void ScriptRunner::readStdout()
{
	if (!m_process) { return; }
	m_stdoutBuffer += m_process->readAllStandardOutput();

	// pass text through immediately, but keep a possibly incomplete marker for the next read
	while (!m_stdoutBuffer.isEmpty())
	{
		const int marker = m_stdoutBuffer.indexOf('\x1e');
		if (marker < 0)
		{
			// keep a character that is split across reads for the next one
			const int complete = completeUtf8Length(m_stdoutBuffer);
			if (complete > 0) { emit output(QString::fromUtf8(m_stdoutBuffer.left(complete)), false); }
			m_stdoutBuffer.remove(0, complete);
			return;
		}
		if (marker > 0)
		{
			emit output(QString::fromUtf8(m_stdoutBuffer.left(marker)), false);
			m_stdoutBuffer.remove(0, marker);
		}
		const int end = m_stdoutBuffer.indexOf('\n');
		if (end < 0) { return; }
		const QByteArray line = m_stdoutBuffer.left(end);
		m_stdoutBuffer.remove(0, end + 1);
		if (line.startsWith(DoneMarker))
		{
			m_busy = false;
			emit executionFinished(line.mid(sizeof(DoneMarker) - 1) == "ok");
		}
		else { emit output(QString::fromUtf8(line) + "\n", false); }
	}
}




void ScriptRunner::readStderr()
{
	if (!m_process) { return; }
	m_stderrBuffer += m_process->readAllStandardError();
	const int complete = completeUtf8Length(m_stderrBuffer);
	if (complete > 0) { emit output(QString::fromUtf8(m_stderrBuffer.left(complete)), true); }
	m_stderrBuffer.remove(0, complete);
}


} // namespace lmms::scripting
