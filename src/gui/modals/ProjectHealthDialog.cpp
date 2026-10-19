/*
 * ProjectHealthDialog.cpp - overview of the files and plugins a project depends on
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

#include "ProjectHealthDialog.h"

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "Engine.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "ProjectResources.h"
#include "Song.h"

namespace lmms::gui
{

namespace
{

using File = ProjectResources::File;

QString kindText(File::Kind kind)
{
	switch (kind)
	{
		case File::Kind::Sample: return ProjectHealthDialog::tr("Sample");
		case File::Kind::Instrument: return ProjectHealthDialog::tr("Instrument file");
		case File::Kind::Plugin: return ProjectHealthDialog::tr("Plugin");
		case File::Kind::Bounce: return ProjectHealthDialog::tr("Bounced audio");
	}
	return {};
}

QString locationText(File::Location location)
{
	switch (location)
	{
		case File::Location::ProjectFolder: return ProjectHealthDialog::tr("Project folder");
		case File::Location::Factory: return ProjectHealthDialog::tr("Comes with LMMS");
		case File::Location::UserFolder: return ProjectHealthDialog::tr("Your LMMS folder");
		case File::Location::Elsewhere: return ProjectHealthDialog::tr("Elsewhere");
	}
	return {};
}

QString pluginList(const std::map<QString, int>& plugins)
{
	QStringList parts;
	for (const auto& [name, count] : plugins) { parts << (count > 1 ? QString("%1 ×%2").arg(name).arg(count) : name); }
	return parts.isEmpty() ? ProjectHealthDialog::tr("none") : parts.join(", ");
}

} // namespace


ProjectHealthDialog::ProjectHealthDialog(QWidget* parent) :
	QDialog(parent),
	m_summary(new QLabel(this)),
	m_files(new QTreeWidget(this)),
	m_plugins(new QLabel(this))
{
	setWindowTitle(tr("Project health"));
	resize(760, 440);

	m_summary->setWordWrap(true);
	m_plugins->setWordWrap(true);
	m_plugins->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_files->setRootIsDecorated(false);
	m_files->setAlternatingRowColors(true);
	m_files->setHeaderLabels({tr("File"), tr("Type"), tr("Location"), tr("Status"), tr("Uses"), tr("Path")});
	m_files->header()->setStretchLastSection(true);

	auto buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	auto bundle = buttons->addButton(tr("Save as Bundle…"), QDialogButtonBox::ActionRole);
	bundle->setToolTip(tr("Save a copy of the project in its own folder, with the samples it uses copied next to it"));
	auto rescan = buttons->addButton(tr("Rescan"), QDialogButtonBox::ActionRole);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(rescan, &QPushButton::clicked, this, &ProjectHealthDialog::refresh);
	connect(bundle, &QPushButton::clicked, this, [this] {
		Engine::getSong()->getSaveOptions().saveAsProjectBundle.setValue(true);
		getGUI()->mainWindow()->saveProjectAs();
		refresh();
	});

	auto layout = new QVBoxLayout(this);
	layout->addWidget(m_summary);
	layout->addWidget(m_files, 1);
	layout->addWidget(m_plugins);
	layout->addWidget(buttons);

	refresh();
}




void ProjectHealthDialog::refresh()
{
	const auto resources = ProjectResources::scanCurrentSong();
	m_files->clear();
	for (const auto& file : resources.files)
	{
		auto item = new QTreeWidgetItem(m_files, {QFileInfo(file.path).fileName(), kindText(file.kind),
			locationText(file.location), file.missing ? tr("Missing") : tr("OK"), QString::number(file.uses),
			file.absolutePath});
		item->setToolTip(5, file.path);
		if (file.missing)
		{
			for (int c = 0; c < item->columnCount(); ++c) { item->setForeground(c, QColor(240, 96, 88)); }
		}
	}
	for (int c = 0; c < 5; ++c) { m_files->resizeColumnToContents(c); }

	QStringList summary;
	summary << tr("%n file(s) outside the project file.", "", static_cast<int>(resources.files.size()));
	if (const int missing = resources.missingCount(); missing > 0)
	{
		summary << tr("<b>%n missing</b>: the project won't sound right until they are found.", "", missing);
	}
	if (const int nonPortable = resources.nonPortableCount(); nonPortable > 0)
	{
		summary << tr("%n file(s) won't travel with the project folder. Save as bundle to collect the samples, "
			"and install the plugins on the other machine.", "", nonPortable);
	}
	if (Engine::getSong()->projectFileName().isEmpty()) { summary << tr("The project hasn't been saved yet."); }
	m_summary->setText(summary.join(" "));
	m_plugins->setText(tr("<b>Instruments:</b> %1<br><b>Effects:</b> %2")
		.arg(pluginList(resources.instruments), pluginList(resources.effects)));
}

} // namespace lmms::gui
