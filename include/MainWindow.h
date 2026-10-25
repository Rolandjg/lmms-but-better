/*
 * MainWindow.h - declaration of class MainWindow, the main window of LMMS
 *
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef LMMS_GUI_MAIN_WINDOW_H
#define LMMS_GUI_MAIN_WINDOW_H

#include <QBasicTimer>
#include <QElapsedTimer>
#include <QTimer>
#include <QList>
#include <QMainWindow>
#include <QMdiArea>

#include "ConfigManager.h"
#include "lmms_export.h"

class QAction;
class QDomElement;
class QGridLayout;

namespace lmms
{

namespace scripting
{
class ScriptRunner;
}

namespace gui
{

class CommandPalette;
class ScriptConsole;
class MidiCapture;
class PluginView;
class SubWindow;
class TilingWindowManager;
class ToolButton;


class LMMS_EXPORT MainWindow : public QMainWindow
{
	Q_OBJECT
public:
	QMdiArea* workspace()
	{
		return static_cast<QMdiArea*>(m_workspace);
	}

	QWidget* toolBar()
	{
		return m_toolBar;
	}

	int addWidgetToToolBar( QWidget * _w, int _row = -1, int _col = -1 );
	void addSpacingToToolBar( int _size );

	// wrap the widget with a window decoration and add it to the workspace
	SubWindow* addWindowedWidget(QWidget *w, Qt::WindowFlags windowFlags = QFlag(0));

	//! The Python Script Console (created on first use)
	ScriptConsole* scriptConsole();
	//! Show the output of a script started from the command line in the Script Console
	void attachScriptRunner(scripting::ScriptRunner* runner);


	void refocus();

	///
	/// \brief	Asks whether changes made to the project are to be saved.
	///
	/// Opens a dialog giving the user the choice to (a) confirm his choice
	/// (such as opening a new file), (b) save the current project before
	/// proceeding or (c) cancel the process.
	///
	/// Every function that replaces the current file (e.g. creates new file,
	/// opens another file...) must call this before and may only proceed if
	/// this function returns true.
	///
	/// \param	stopPlayback whether playback should be stopped upon prompting.  If set to false, the caller should ensure that Engine::getSong()->stop() is called before unloading/loading a song.
	///
	/// \return	true if the user allows the software to proceed, false if they
	///         cancel the action.
	///
	bool mayChangeProject(bool stopPlayback);

	// Auto save timer intervals. The slider in SetupDialog.cpp wants
	// minutes and the rest milliseconds.
	static const int DEFAULT_SAVE_INTERVAL_MINUTES = 2;
	static const int DEFAULT_AUTO_SAVE_INTERVAL = DEFAULT_SAVE_INTERVAL_MINUTES * 60 * 1000;

	static const int m_autoSaveShortTime = 10 * 1000; // 10s short loop

	//! The regular auto save interval in milliseconds
	static int autoSaveInterval()
	{
		const int msec = ConfigManager::inst()->value("ui", "saveinterval").toInt() * 60 * 1000;
		return msec < m_autoSaveShortTime ? DEFAULT_AUTO_SAVE_INTERVAL : msec;
	}

	void autoSaveTimerReset( int msec = autoSaveInterval() )
	{
		if( msec < m_autoSaveShortTime ) // No 'saveinterval' in .lmmsrc.xml
		{
			msec = DEFAULT_AUTO_SAVE_INTERVAL;
		}
		m_autoSaveTimer.start( msec );
	}

	int getAutoSaveTimerInterval()
	{
		return m_autoSaveTimer.interval();
	}

	enum class SessionState
	{
		Normal,
		Recover
	};

	void setSession( SessionState session )
	{
		m_session = session;
	}

	SessionState getSession()
	{
		return m_session;
	}

	void sessionCleanup();

	void clearKeyModifiers();

	bool isShiftPressed() const { return m_keyMods.m_shift; }

	static void saveWidgetState( QWidget * _w, QDomElement & _de );
	static void restoreWidgetState( QWidget * _w, const QDomElement & _de );
	void setAllSubWindowsDetached(bool detached);

	bool eventFilter(QObject* watched, QEvent* event) override;

signals:
	void detachAllSubWindows(bool detached);

public slots:
	void resetWindowTitle();

	void emptySlot();
	void createNewProject();
	void openProject();
	bool saveProject();
	bool saveProjectAs();
	bool saveProjectAsNewVersion();
	void saveProjectAsDefaultTemplate();
	void showSettingsDialog();
	void showCommandPalette();
	void captureMidi();
	void aboutLMMS();
	void help();
	void toggleAutomationEditorWin();
	void togglePatternEditorWin(bool forceShow = false);
	void toggleSongEditorWin();
	void toggleProjectNotesWin();
	void toggleMicrotunerWin();
	void toggleScriptConsole();
	void toggleMixerWin();
	void togglePianoRollWin();
	void toggleControllerRack();
	void toggleFullscreen();

	void updatePlayPauseIcons();

	void updateUndoRedoButtons();
	void undo();
	void redo();

	void autoSave();

private slots:
	void onExportProjectMidi();

protected:
	void closeEvent( QCloseEvent * _ce ) override;
	void keyPressEvent( QKeyEvent * _ke ) override;
	void keyReleaseEvent( QKeyEvent * _ke ) override;
	void timerEvent( QTimerEvent * _ev ) override;


private:
	MainWindow();
	MainWindow( const MainWindow & );
	~MainWindow() override;

	void finalize();

	void toggleWindow( QWidget *window, bool forceShow = false );

	void exportProject(bool multiExport = false);
	void handleSaveResult(QString const & filename, bool songSavedSuccessfully);
	bool guiSaveProject();
	bool guiSaveProjectAs( const QString & filename );

	class MovableQMdiArea : public QMdiArea
	{
	public:
		MovableQMdiArea(QWidget* parent = nullptr);
		~MovableQMdiArea() {}
	protected:
		void mousePressEvent(QMouseEvent* event) override;
		void mouseMoveEvent(QMouseEvent* event) override;
		void mouseReleaseEvent(QMouseEvent* event) override;
	private:
		bool m_isBeingMoved;
		int m_lastX;
		int m_lastY;
	};

	MovableQMdiArea * m_workspace;
	//! Tiles the workspace windows when "Tile windows" is on
	TilingWindowManager* m_tiling;

	QWidget * m_toolBar;
	QGridLayout * m_toolBarLayout;

	struct keyModifiers
	{
		keyModifiers() :
			m_ctrl( false ),
			m_shift( false ),
			m_alt( false )
		{
		}
		bool m_ctrl;
		bool m_shift;
		bool m_alt;
	} m_keyMods;

	QMenu * m_toolsMenu;
	QAction * m_undoAction;
	QAction * m_redoAction;
	QList<PluginView *> m_tools;

	QBasicTimer m_updateTimer;
	QTimer m_autoSaveTimer;
	int m_autoSaveInterval;
	QElapsedTimer m_autoSaveSkipped; //!< runs while auto saves are being postponed
	bool m_autoSaveStalled = false; //!< auto save is overdue and can't run, the user gets warned

	void setAutoSaveStalled(bool stalled);

	friend class GuiApplication;

	QMenu * m_viewMenu;

	ToolButton * m_metronomeToggle;
	CommandPalette* m_commandPalette = nullptr;
	MidiCapture* m_midiCapture = nullptr;
	ScriptConsole* m_scriptConsole = nullptr;
	QAction* m_scriptConsoleAction = nullptr;

	SessionState m_session;
	
	bool maximized;

private slots:
	void browseHelp();
	void showTool( QAction * _idx );
	void updateViewMenu();
	void updateConfig( QAction * _who );
	void onToggleMetronome();
	void onExportProject();
	void onExportProjectTracks();
	void onImportProject();
	void onSongModified();
	void onProjectFileNameChanged();

signals:
	void periodicUpdate();
	void initProgress(const QString &msg);

} ;

} // namespace gui

} // namespace lmms

#endif // LMMS_GUI_MAIN_WINDOW_H
