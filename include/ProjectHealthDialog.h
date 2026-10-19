/*
 * ProjectHealthDialog.h - overview of the files and plugins a project depends on
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

#ifndef LMMS_GUI_PROJECT_HEALTH_DIALOG_H
#define LMMS_GUI_PROJECT_HEALTH_DIALOG_H

#include <QDialog>

class QLabel;
class QTreeWidget;

namespace lmms::gui
{

//! Lists every sample, instrument file and plugin binary the project uses, which are missing and which
//! won't travel with the project folder, and offers to save a self-contained bundle
class ProjectHealthDialog : public QDialog
{
	Q_OBJECT
public:
	explicit ProjectHealthDialog(QWidget* parent = nullptr);

	void refresh();

private:
	QLabel* m_summary;
	QTreeWidget* m_files;
	QLabel* m_plugins;
};

} // namespace lmms::gui

#endif // LMMS_GUI_PROJECT_HEALTH_DIALOG_H
