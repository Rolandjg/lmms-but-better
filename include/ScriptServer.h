/*
 * ScriptServer.h - local socket server that lets scripts and agents drive LMMS
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

#ifndef LMMS_SCRIPT_SERVER_H
#define LMMS_SCRIPT_SERVER_H

#include <QJsonObject>
#include <QObject>
#include <QProcessEnvironment>

#include "lmms_export.h"

class QTcpServer;
class QTcpSocket;

namespace lmms::scripting
{

/**
	Listens on 127.0.0.1 for newline-delimited JSON requests and runs them through ScriptApi.

	A client first sends {"token": "..."}; afterwards each line is {"id", "method", "params"} and is
	answered with {"id", "result"} or {"id", "error": {"message"}}. The port and token are written to
	connectionFile() (readable by the current user only) and passed to child scripts via environment.
*/
class LMMS_EXPORT ScriptServer : public QObject
{
	Q_OBJECT
public:
	static ScriptServer* instance();

	//! Start listening (no-op when already listening). @p port 0 picks a free port.
	bool start(quint16 port = 0);
	void stop();

	bool isListening() const;
	quint16 port() const;
	const QString& token() const { return m_token; }
	int clientCount() const { return m_clients; }

	//! {"host", "port", "token", "pid"}
	QJsonObject connectionInfo() const;
	//! Where connectionInfo() is stored while listening
	static QString connectionFile();

	//! Adds LMMS_SCRIPT_HOST/PORT/TOKEN so a child process can connect
	void addToEnvironment(QProcessEnvironment& env) const;

signals:
	void clientCountChanged(int count);

private:
	ScriptServer();
	~ScriptServer() override;

	void onNewConnection();
	void handleLine(QTcpSocket* socket, const QByteArray& line);

	QTcpServer* m_server = nullptr;
	QString m_token;
	int m_clients = 0;
};

} // namespace lmms::scripting

#endif // LMMS_SCRIPT_SERVER_H
