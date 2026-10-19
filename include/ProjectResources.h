/*
 * ProjectResources.h - the files a project depends on
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

#ifndef LMMS_PROJECT_RESOURCES_H
#define LMMS_PROJECT_RESOURCES_H

#include <map>
#include <vector>

#include <QString>

#include "lmms_export.h"

class QDomDocument;

namespace lmms
{

//! Everything outside the project file that a project needs: what "Project health" shows, so users know what
//! has to travel with a project to another machine
struct LMMS_EXPORT ProjectResources
{
	struct File
	{
		enum class Kind { Sample, Instrument, Plugin, Bounce };
		//! Where the file lives, which decides whether it travels with the project
		enum class Location
		{
			ProjectFolder, //!< next to the project (travels with the folder)
			Factory,       //!< ships with LMMS
			UserFolder,    //!< in the user's LMMS folder (must be copied to another machine)
			Elsewhere      //!< any other absolute path
		};

		Kind kind = Kind::Sample;
		QString path;          //!< as stored in the project
		QString absolutePath;
		Location location = Location::Elsewhere;
		bool missing = false;
		int uses = 0;
	};

	std::vector<File> files;
	std::map<QString, int> instruments; //!< plugin name -> number of tracks using it
	std::map<QString, int> effects;     //!< plugin name -> number of instances

	//! Scans a serialized project. @p projectFile (may be empty) is where it is saved.
	static ProjectResources scan(const QDomDocument& project, const QString& projectFile);
	//! Scans the current song
	static ProjectResources scanCurrentSong();

	int missingCount() const;
	//! Files that don't travel with the project folder: user folder or elsewhere
	int nonPortableCount() const;
};

} // namespace lmms

#endif // LMMS_PROJECT_RESOURCES_H
