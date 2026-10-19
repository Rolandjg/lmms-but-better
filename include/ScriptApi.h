/*
 * ScriptApi.h - the command set exposed to scripts (Python module "lmms", agents, ...)
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

#ifndef LMMS_SCRIPT_API_H
#define LMMS_SCRIPT_API_H

#include <functional>
#include <map>
#include <memory>
#include <stdexcept>

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QPointer>
#include <QTemporaryDir>

#include "lmms_export.h"

namespace lmms
{

class AutomatableModel;

namespace scripting
{

//! Thrown by command handlers; the message is sent back to the script
class ScriptError : public std::runtime_error
{
public:
	explicit ScriptError(const QString& message) : std::runtime_error(message.toStdString()) {}
};


/**
	Dispatches JSON commands ("song.info", "track.add_clip", ...) to the LMMS core.

	All calls run on the GUI/main thread. Objects (tracks, clips, plugins, parameters) are
	referred to by integer handles that stay valid for the lifetime of the object.
	Positions and lengths are in beats (quarter notes, 48 ticks).
*/
class LMMS_EXPORT ScriptApi : public QObject
{
	Q_OBJECT
public:
	//! Reply callback: exactly one of @p result / @p error is meaningful (error is non-empty on failure)
	using Reply = std::function<void(const QJsonValue& result, const QString& error)>;

	static ScriptApi* instance();

	//! Runs @p method; @p reply may be invoked later for long-running commands (e.g. rendering)
	void call(const QString& method, const QJsonObject& params, const Reply& reply);

	//! Method name -> one-line description
	QJsonObject describe() const;

	//! Save the project to a temporary checkpoint (scripts call song.checkpoint); returns its id
	int createCheckpoint();
	//! Put the project back to checkpoint @p id (0 = the latest); throws ScriptError if there is none
	void restoreCheckpoint(int id = 0);
	bool hasCheckpoint(int id = 0) const;

	//! Handle for @p object (allocated on first use)
	int handleFor(QObject* object);
	//! Object for @p handle, nullptr if unknown or deleted
	QObject* objectFor(int handle) const;

signals:
	//! Emitted when a script asks LMMS to quit
	void quitRequested(int exitCode);
	//! Text a script asked to be shown to the user (lmms.message)
	void messagePosted(const QString& text);

private:
	ScriptApi();

	using SyncHandler = std::function<QJsonValue(const QJsonObject&)>;
	using AsyncHandler = std::function<void(const QJsonObject&, const Reply&)>;

	struct Method
	{
		QString help;
		SyncHandler sync;
		AsyncHandler async;
	};

	void add(const QString& name, const QString& help, SyncHandler handler);
	void addAsync(const QString& name, const QString& help, AsyncHandler handler);

	void registerSongMethods();
	void registerTrackMethods();
	void registerClipMethods();
	void registerPluginMethods();
	void registerMixerMethods();
	void registerMiscMethods();

	//! True while a render started by song.render (or the GUI) is running
	bool renderInProgress() const;

	std::map<QString, Method> m_methods;
	QHash<int, QPointer<QObject>> m_objects;
	QHash<QObject*, int> m_handles;
	int m_nextHandle = 1;
	int m_activeRenders = 0;

	std::unique_ptr<QTemporaryDir> m_checkpointDir;
	std::map<int, QString> m_checkpoints; //!< id -> file
	int m_nextCheckpoint = 1;
};


} // namespace scripting
} // namespace lmms

#endif // LMMS_SCRIPT_API_H
