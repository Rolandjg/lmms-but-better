/*
 * ProjectResources.cpp - the files a project depends on
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

#include "ProjectResources.h"

#include <algorithm>
#include <array>

#include <QDir>
#include <QDomDocument>
#include <QFileInfo>

#include "DataFile.h"
#include "Engine.h"
#include "PathUtil.h"
#include "Song.h"

namespace lmms
{

namespace
{

using File = ProjectResources::File;

struct ResourceAttribute
{
	const char* element;
	const char* attribute;
	File::Kind kind;
};

// Elements (instruments, clips, plugin controls) and the attributes that point to files
constexpr auto ResourceAttributes = std::array{
	ResourceAttribute{"sampleclip", "src", File::Kind::Sample},
	ResourceAttribute{"audiofileprocessor", "src", File::Kind::Sample},
	ResourceAttribute{"slicert", "src", File::Kind::Sample},
	ResourceAttribute{"patman", "src", File::Kind::Instrument},
	ResourceAttribute{"sf2player", "src", File::Kind::Instrument},
	ResourceAttribute{"gigplayer", "src", File::Kind::Instrument},
	ResourceAttribute{"vestige", "plugin", File::Kind::Plugin},
	ResourceAttribute{"vsteffectcontrols", "plugin", File::Kind::Plugin},
	ResourceAttribute{"vst3instrument", "plugin-file", File::Kind::Plugin},
	ResourceAttribute{"vst3controls", "plugin-file", File::Kind::Plugin},
};

bool isInside(const QString& path, const QString& dir)
{
	if (dir.isEmpty()) { return false; }
	const auto relative = QDir(dir).relativeFilePath(path);
	return !relative.startsWith("..") && !QDir::isAbsolutePath(relative);
}

File::Location locate(const QString& stored, const QString& absolute, const QString& projectDir)
{
	using Base = PathUtil::Base;
	switch (PathUtil::baseLookup(stored))
	{
		case Base::FactoryProjects:
		case Base::FactorySample:
		case Base::FactoryPresets:
		case Base::DefaultLADSPA:
		case Base::DefaultSoundfont:
		case Base::DefaultGIG:
			return File::Location::Factory;
		case Base::ProjectDir:
		case Base::LocalDir:
			return File::Location::ProjectFolder;
		case Base::Absolute:
			break;
		default:
			return isInside(absolute, projectDir) ? File::Location::ProjectFolder : File::Location::UserFolder;
	}
	if (isInside(absolute, projectDir)) { return File::Location::ProjectFolder; }
	for (const auto base : {Base::FactorySample, Base::FactoryProjects, Base::FactoryPresets, Base::DefaultSoundfont})
	{
		if (isInside(absolute, PathUtil::baseLocation(base))) { return File::Location::Factory; }
	}
	return File::Location::Elsewhere;
}

} // namespace


ProjectResources ProjectResources::scan(const QDomDocument& project, const QString& projectFile)
{
	ProjectResources result;
	const auto projectDir = projectFile.isEmpty() ? QString{} : QFileInfo(projectFile).absolutePath();
	for (const auto& resource : ResourceAttributes)
	{
		const auto elements = project.elementsByTagName(resource.element);
		for (int i = 0; i < elements.count(); ++i)
		{
			const auto stored = elements.item(i).toElement().attribute(resource.attribute);
			if (stored.isEmpty()) { continue; }
			const auto existing = std::find_if(result.files.begin(), result.files.end(),
				[&](const File& f) { return f.path == stored; });
			if (existing != result.files.end()) { ++existing->uses; continue; }

			File file;
			file.path = stored;
			file.absolutePath = PathUtil::toAbsolute(stored);
			if (QFileInfo(file.absolutePath).isRelative() && !projectDir.isEmpty())
			{
				file.absolutePath = QDir(projectDir).absoluteFilePath(file.absolutePath);
			}
			file.kind = resource.kind == File::Kind::Sample && QFileInfo(stored).fileName().contains("-bounce-")
				? File::Kind::Bounce : resource.kind;
			file.location = locate(stored, file.absolutePath, projectDir);
			file.missing = !QFileInfo::exists(file.absolutePath);
			file.uses = 1;
			result.files.push_back(file);
		}
	}

	const auto count = [&](const char* tag, std::map<QString, int>& into) {
		const auto elements = project.elementsByTagName(tag);
		for (int i = 0; i < elements.count(); ++i)
		{
			const auto name = elements.item(i).toElement().attribute("name");
			if (!name.isEmpty()) { ++into[name]; }
		}
	};
	count("instrument", result.instruments);
	count("effect", result.effects);
	return result;
}




ProjectResources ProjectResources::scanCurrentSong()
{
	auto song = Engine::getSong();
	return scan(song->toDataFile(), song->projectFileName());
}




int ProjectResources::missingCount() const
{
	return static_cast<int>(std::count_if(files.begin(), files.end(), [](const File& f) { return f.missing; }));
}




int ProjectResources::nonPortableCount() const
{
	return static_cast<int>(std::count_if(files.begin(), files.end(), [](const File& f) {
		return f.location == File::Location::UserFolder || f.location == File::Location::Elsewhere;
	}));
}

} // namespace lmms
