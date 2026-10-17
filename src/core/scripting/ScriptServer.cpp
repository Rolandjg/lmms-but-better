/*
 * ScriptServer.cpp - local socket server that lets scripts and agents drive LMMS
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

#include "ScriptServer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QPointer>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>

#include "ScriptApi.h"
#include "lmmsversion.h"

namespace lmms::scripting
{

namespace
{

constexpr int MaxLineLength = 64 * 1024 * 1024;

void writeJson(QTcpSocket* socket, const QJsonObject& message)
{
	if (!socket || socket->state() != QAbstractSocket::ConnectedState) { return; }
	socket->write(QJsonDocument(message).toJson(QJsonDocument::Compact));
	socket->write("\n");
	socket->flush();
}

} // namespace




ScriptServer* ScriptServer::instance()
{
	static ScriptServer* server = new ScriptServer;
	return server;
}




ScriptServer::ScriptServer()
{
	// 128 random bits are plenty for a token only reachable from this machine
	for (int i = 0; i < 4; ++i)
	{
		m_token += QString("%1").arg(QRandomGenerator::system()->generate(), 8, 16, QChar('0'));
	}
}




ScriptServer::~ScriptServer()
{
	stop();
}




bool ScriptServer::start(quint16 port)
{
	if (isListening()) { return true; }
	if (!m_server)
	{
		m_server = new QTcpServer(this);
		connect(m_server, &QTcpServer::newConnection, this, &ScriptServer::onNewConnection);
		connect(qApp, &QCoreApplication::aboutToQuit, this, &ScriptServer::stop);
	}
	if (!m_server->listen(QHostAddress::LocalHost, port))
	{
		qWarning("Script server: cannot listen on port %d: %s", port, qPrintable(m_server->errorString()));
		return false;
	}

	QFile file(connectionFile());
	if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
		file.write(QJsonDocument(connectionInfo()).toJson());
	}
	return true;
}




void ScriptServer::stop()
{
	if (!isListening()) { return; }
	m_server->close();

	// only remove the connection file if another instance hasn't replaced it
	QFile file(connectionFile());
	if (file.open(QIODevice::ReadOnly))
	{
		const auto info = QJsonDocument::fromJson(file.readAll()).object();
		file.close();
		if (info.value("pid").toInt() == QCoreApplication::applicationPid()) { file.remove(); }
	}
}




bool ScriptServer::isListening() const
{
	return m_server && m_server->isListening();
}




quint16 ScriptServer::port() const
{
	return isListening() ? m_server->serverPort() : 0;
}




QJsonObject ScriptServer::connectionInfo() const
{
	QJsonObject o;
	o["host"] = "127.0.0.1";
	o["port"] = port();
	o["token"] = m_token;
	o["pid"] = static_cast<int>(QCoreApplication::applicationPid());
	o["version"] = LMMS_VERSION;
	return o;
}




QString ScriptServer::connectionFile()
{
	QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
	if (dir.isEmpty()) { dir = QDir::tempPath(); }
	return QDir(dir).filePath("lmms-script-server.json");
}




void ScriptServer::addToEnvironment(QProcessEnvironment& env) const
{
	env.insert("LMMS_SCRIPT_HOST", "127.0.0.1");
	env.insert("LMMS_SCRIPT_PORT", QString::number(port()));
	env.insert("LMMS_SCRIPT_TOKEN", m_token);
}




void ScriptServer::onNewConnection()
{
	while (QTcpSocket* socket = m_server->nextPendingConnection())
	{
		socket->setProperty("authenticated", false);
		++m_clients;
		emit clientCountChanged(m_clients);

		connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
			QByteArray buffer = socket->property("buffer").toByteArray() + socket->readAll();
			int newline;
			while ((newline = buffer.indexOf('\n')) >= 0)
			{
				const QByteArray line = buffer.left(newline).trimmed();
				buffer.remove(0, newline + 1);
				if (!line.isEmpty()) { handleLine(socket, line); }
				if (socket->state() != QAbstractSocket::ConnectedState) { return; }
			}
			if (buffer.size() > MaxLineLength)
			{
				writeJson(socket, {{"error", QJsonObject{{"message", "request too large"}}}});
				socket->disconnectFromHost();
				return;
			}
			socket->setProperty("buffer", buffer);
		});
		connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
			socket->deleteLater();
			--m_clients;
			emit clientCountChanged(m_clients);
		});
	}
}




void ScriptServer::handleLine(QTcpSocket* socket, const QByteArray& line)
{
	QJsonParseError parseError;
	const auto doc = QJsonDocument::fromJson(line, &parseError);
	if (!doc.isObject())
	{
		writeJson(socket, {{"error", QJsonObject{{"message", "invalid JSON: " + parseError.errorString()}}}});
		return;
	}
	const QJsonObject request = doc.object();

	if (!socket->property("authenticated").toBool())
	{
		if (request.value("token").toString() != m_token)
		{
			writeJson(socket, {{"error", QJsonObject{{"message", "authentication failed: send {\"token\": ...} first"}}}});
			socket->disconnectFromHost();
			return;
		}
		socket->setProperty("authenticated", true);
		writeJson(socket, {{"ok", true}, {"version", LMMS_VERSION}});
		return;
	}

	const QJsonValue id = request.value("id");
	QPointer<QTcpSocket> target = socket;
	ScriptApi::instance()->call(request.value("method").toString(), request.value("params").toObject(),
		[target, id](const QJsonValue& result, const QString& error) {
			QJsonObject reply;
			reply["id"] = id;
			if (error.isEmpty()) { reply["result"] = result; }
			else { reply["error"] = QJsonObject{{"message", error}}; }
			writeJson(target, reply);
		});
}


} // namespace lmms::scripting
